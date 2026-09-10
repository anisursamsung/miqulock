#include "lock_app.hpp"
#include "lock_surface.hpp"
#include "auth.hpp"
#include "config.hpp"
#include "ext-session-lock-v1-client-protocol.h"

#include <sys/mman.h>
#include <sys/timerfd.h>
#include <sys/signalfd.h>
#include <sys/inotify.h>
#include <signal.h>
#include <poll.h>
#include <unistd.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace miqulock {

static const struct wl_registry_listener registry_listener = {
    .global = [](void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
        auto* app = static_cast<LockApp*>(data);
        app->handle_global(registry, name, interface, version);
    },
    .global_remove = [](void* data, struct wl_registry*, uint32_t name) {
        auto* app = static_cast<LockApp*>(data);
        app->handle_global_remove(name);
    }
};

static const struct ext_session_lock_v1_listener lock_listener = {
    .locked = [](void*, struct ext_session_lock_v1*) {
        std::cout << "[miqulock] Session successfully locked." << std::endl;
    },
    .finished = [](void* data, struct ext_session_lock_v1*) {
        auto* app = static_cast<LockApp*>(data);
        std::cerr << "[miqulock] Session lock finished/denied by compositor." << std::endl;
        app->quit();
    }
};

static const struct wl_seat_listener seat_listener = {
    .capabilities = [](void* data, struct wl_seat*, uint32_t caps) {
        auto* app = static_cast<LockApp*>(data);
        app->handle_seat_caps(caps);
    },
    .name = [](void*, struct wl_seat*, const char*) {}
};

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = [](void* data, struct wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
        auto* app = static_cast<LockApp*>(data);
        app->handle_keymap(format, fd, size);
    },
    .enter = [](void*, struct wl_keyboard*, uint32_t, struct wl_surface*, struct wl_array*) {},
    .leave = [](void*, struct wl_keyboard*, uint32_t, struct wl_surface*) {},
    .key = [](void* data, struct wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
        auto* app = static_cast<LockApp*>(data);
        app->handle_key_event(key, state);
    },
    .modifiers = [](void* data, struct wl_keyboard*, uint32_t, uint32_t mods_depressed,
                    uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
        auto* app = static_cast<LockApp*>(data);
        app->handle_modifiers(mods_depressed, mods_latched, mods_locked, group);
    },
    .repeat_info = [](void*, struct wl_keyboard*, int32_t, int32_t) {}
};

LockApp::LockApp() {
    m_xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    m_auth = std::make_unique<AuthManager>();
}

LockApp::~LockApp() {
    clear_password();

    m_surfaces.clear();
    for (auto& out : m_outputs) {
        wl_output_destroy(out.second);
    }
    m_outputs.clear();

    if (m_lock) {
        ext_session_lock_v1_destroy(m_lock);
        m_lock = nullptr;
    }
    if (m_lock_manager) {
        ext_session_lock_manager_v1_destroy(m_lock_manager);
        m_lock_manager = nullptr;
    }
    if (m_keyboard) {
        wl_keyboard_destroy(m_keyboard);
        m_keyboard = nullptr;
    }
    if (m_seat) {
        wl_seat_destroy(m_seat);
        m_seat = nullptr;
    }
    if (m_shm) {
        wl_shm_destroy(m_shm);
        m_shm = nullptr;
    }
    if (m_compositor) {
        wl_compositor_destroy(m_compositor);
        m_compositor = nullptr;
    }
    if (m_registry) {
        wl_registry_destroy(m_registry);
        m_registry = nullptr;
    }
    if (m_display) {
        wl_display_disconnect(m_display);
        m_display = nullptr;
    }
    if (m_xkb_state) xkb_state_unref(m_xkb_state);
    if (m_xkb_keymap) xkb_keymap_unref(m_xkb_keymap);
    if (m_xkb_context) xkb_context_unref(m_xkb_context);

    if (m_timer_fd >= 0) {
        close(m_timer_fd);
        m_timer_fd = -1;
    }

    cleanup_inotify();
    cleanup_signals();
}

void LockApp::clear_password() {
    if (!m_password.empty()) {
        explicit_bzero(m_password.data(), m_password.size());
        m_password.clear();
    }
}

std::string LockApp::get_username() const {
    return m_auth ? m_auth->get_current_username() : "User";
}

bool LockApp::is_verifying() const {
    return m_auth ? m_auth->is_authenticating() : false;
}

double LockApp::get_shake_offset() const {
    if (!m_auth_failed) return 0.0;

    auto now = std::chrono::steady_clock::now();
    double elapsed_sec = std::chrono::duration<double>(now - m_fail_time).count();
    if (elapsed_sec > 0.4) {
        return 0.0;
    }

    double freq = 35.0;
    double decay = (1.0 - elapsed_sec / 0.4);
    return std::sin(elapsed_sec * freq) * 14.0 * decay;
}

void LockApp::handle_global(struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    if (std::string(interface) == "wl_compositor") {
        m_compositor = static_cast<struct wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
    } else if (std::string(interface) == "wl_shm") {
        m_shm = static_cast<struct wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (std::string(interface) == "wl_seat") {
        m_seat = static_cast<struct wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 7u)));
        wl_seat_add_listener(m_seat, &seat_listener, this);
    } else if (std::string(interface) == "wl_output") {
        auto* output = static_cast<struct wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 4u)));
        m_outputs.push_back({ name, output });

        // If session is already locked, immediately bind LockSurface for new output (Hotplugging)
        if (m_lock) {
            auto surface = std::make_unique<LockSurface>(this, output);
            surface->init();
            m_surfaces.push_back(std::move(surface));
            wl_display_flush(m_display);
        }
    } else if (std::string(interface) == "ext_session_lock_manager_v1") {
        m_lock_manager = static_cast<struct ext_session_lock_manager_v1*>(
            wl_registry_bind(registry, name, &ext_session_lock_manager_v1_interface, 1));
    }
}

void LockApp::handle_global_remove(uint32_t name) {
    for (auto it = m_outputs.begin(); it != m_outputs.end(); ++it) {
        if (it->first == name) {
            struct wl_output* out = it->second;
            m_surfaces.erase(
                std::remove_if(m_surfaces.begin(), m_surfaces.end(),
                    [out](const std::unique_ptr<LockSurface>& s) {
                        return s->get_wl_output() == out;
                    }),
                m_surfaces.end()
            );
            wl_output_destroy(out);
            m_outputs.erase(it);
            break;
        }
    }
}

void LockApp::handle_seat_caps(uint32_t caps) {
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !m_keyboard) {
        m_keyboard = wl_seat_get_keyboard(m_seat);
        wl_keyboard_add_listener(m_keyboard, &keyboard_listener, this);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && m_keyboard) {
        wl_keyboard_destroy(m_keyboard);
        m_keyboard = nullptr;
    }
}

void LockApp::handle_keymap(uint32_t format, int32_t fd, uint32_t size) {
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char* map_shm = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0));
    if (map_shm == MAP_FAILED) {
        close(fd);
        return;
    }

    if (m_xkb_keymap) xkb_keymap_unref(m_xkb_keymap);
    if (m_xkb_state) xkb_state_unref(m_xkb_state);

    m_xkb_keymap = xkb_keymap_new_from_string(
        m_xkb_context, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_shm, size);
    close(fd);

    if (m_xkb_keymap) {
        m_xkb_state = xkb_state_new(m_xkb_keymap);
    }
}

void LockApp::handle_key_event(uint32_t key, uint32_t state) {
    on_key(key, state);
}

void LockApp::handle_modifiers(uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
    if (m_xkb_state) {
        xkb_state_update_mask(m_xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
        bool caps = xkb_state_mod_name_is_active(m_xkb_state, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_LOCKED) > 0;
        if (caps != m_caps_lock_active) {
            m_caps_lock_active = caps;
            redraw_all();
        }
    }
}

void LockApp::setup_timer() {
    m_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (m_timer_fd < 0) {
        std::cerr << "[miqulock] Warning: Failed to create timerfd: " << strerror(errno) << "\n";
        return;
    }
    set_timer_interval_ms(1000); // Default 1-second clock tick
}

void LockApp::set_timer_interval_ms(int ms) {
    if (m_timer_fd < 0) return;

    struct itimerspec its{};
    its.it_interval.tv_sec = ms / 1000;
    its.it_interval.tv_nsec = (ms % 1000) * 1000000LL;
    its.it_value = its.it_interval;
    if (its.it_value.tv_sec == 0 && its.it_value.tv_nsec == 0) {
        its.it_value.tv_nsec = 1;
    }
    timerfd_settime(m_timer_fd, 0, &its, nullptr);
}

void LockApp::setup_signals() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGUSR1);

    if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
        std::cerr << "[miqulock] Warning: Failed to block signals: " << strerror(errno) << "\n";
    }

    m_signal_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (m_signal_fd < 0) {
        std::cerr << "[miqulock] Warning: Failed to create signalfd: " << strerror(errno) << "\n";
    }
}

void LockApp::cleanup_signals() {
    if (m_signal_fd >= 0) {
        close(m_signal_fd);
        m_signal_fd = -1;
    }
}

void LockApp::setup_inotify() {
    const std::string& config_path = Config::get().get_config_path();
    if (config_path.empty()) return;

    std::filesystem::path cfg(config_path);
    std::filesystem::path dir = cfg.parent_path();
    if (dir.empty()) dir = ".";

    m_inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_inotify_fd < 0) return;

    if (std::filesystem::exists(dir)) {
        m_inotify_dir_wd = inotify_add_watch(m_inotify_fd, dir.c_str(),
            IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
    }
    if (std::filesystem::exists(cfg)) {
        m_inotify_file_wd = inotify_add_watch(m_inotify_fd, cfg.c_str(),
            IN_MODIFY | IN_CLOSE_WRITE);
    }
}

void LockApp::cleanup_inotify() {
    if (m_inotify_fd >= 0) {
        if (m_inotify_file_wd >= 0) inotify_rm_watch(m_inotify_fd, m_inotify_file_wd);
        if (m_inotify_dir_wd >= 0) inotify_rm_watch(m_inotify_fd, m_inotify_dir_wd);
        close(m_inotify_fd);
        m_inotify_fd = -1;
        m_inotify_file_wd = -1;
        m_inotify_dir_wd = -1;
    }
}

void LockApp::handle_inotify() {
    char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t len;
    bool should_reload = false;
    std::string target_file = std::filesystem::path(Config::get().get_config_path()).filename().string();

    while ((len = read(m_inotify_fd, buffer, sizeof(buffer))) > 0) {
        for (char* ptr = buffer; ptr < buffer + len; ) {
            auto* event = reinterpret_cast<const struct inotify_event*>(ptr);
            if (event->wd == m_inotify_file_wd) {
                should_reload = true;
            } else if (event->wd == m_inotify_dir_wd && event->len > 0) {
                if (event->name == target_file) should_reload = true;
            }
            ptr += sizeof(struct inotify_event) + event->len;
        }
    }

    if (should_reload) {
        const std::string& config_path = Config::get().get_config_path();
        if (std::filesystem::exists(config_path)) {
            if (m_inotify_file_wd >= 0) inotify_rm_watch(m_inotify_fd, m_inotify_file_wd);
            m_inotify_file_wd = inotify_add_watch(m_inotify_fd, config_path.c_str(),
                IN_MODIFY | IN_CLOSE_WRITE);
        }
        Config::get().reload();
        redraw_all();
    }
}

bool LockApp::init() {
    m_display = wl_display_connect(nullptr);
    if (!m_display) {
        std::cerr << "[miqulock] Failed to connect to Wayland display." << std::endl;
        return false;
    }

    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &registry_listener, this);
    wl_display_roundtrip(m_display);

    if (!m_compositor || !m_shm || !m_lock_manager) {
        std::cerr << "[miqulock] Missing required Wayland globals (compositor, shm, or ext_session_lock_manager_v1)." << std::endl;
        return false;
    }

    setup_timer();
    setup_signals();
    setup_inotify();

    m_lock = ext_session_lock_manager_v1_lock(m_lock_manager);
    ext_session_lock_v1_add_listener(m_lock, &lock_listener, this);

    for (const auto& out_pair : m_outputs) {
        auto surface = std::make_unique<LockSurface>(this, out_pair.second);
        surface->init();
        m_surfaces.push_back(std::move(surface));
    }

    wl_display_roundtrip(m_display);
    return true;
}

void LockApp::run() {
    while (m_running) {
        while (wl_display_prepare_read(m_display) != 0) {
            wl_display_dispatch_pending(m_display);
        }
        wl_display_flush(m_display);

        struct pollfd pfd[4];
        int nfds = 1;

        pfd[0].fd = wl_display_get_fd(m_display);
        pfd[0].events = POLLIN;
        pfd[0].revents = 0;

        int timer_idx = -1;
        if (m_timer_fd >= 0) {
            timer_idx = nfds++;
            pfd[timer_idx].fd = m_timer_fd;
            pfd[timer_idx].events = POLLIN;
            pfd[timer_idx].revents = 0;
        }

        int signal_idx = -1;
        if (m_signal_fd >= 0) {
            signal_idx = nfds++;
            pfd[signal_idx].fd = m_signal_fd;
            pfd[signal_idx].events = POLLIN;
            pfd[signal_idx].revents = 0;
        }

        int inotify_idx = -1;
        if (m_inotify_fd >= 0) {
            inotify_idx = nfds++;
            pfd[inotify_idx].fd = m_inotify_fd;
            pfd[inotify_idx].events = POLLIN;
            pfd[inotify_idx].revents = 0;
        }

        int ret = poll(pfd, nfds, -1);
        if (ret < 0) {
            if (errno == EINTR) {
                wl_display_cancel_read(m_display);
                continue;
            }
            wl_display_cancel_read(m_display);
            break;
        }

        if (pfd[0].revents & POLLIN) {
            wl_display_read_events(m_display);
        } else {
            wl_display_cancel_read(m_display);
        }

        wl_display_dispatch_pending(m_display);

        // Timer Tick (1-second for clock, 16ms for shake animation)
        if (timer_idx >= 0 && (pfd[timer_idx].revents & POLLIN)) {
            uint64_t expirations = 0;
            read(m_timer_fd, &expirations, sizeof(expirations));

            if (m_auth_failed) {
                auto now = std::chrono::steady_clock::now();
                double elapsed_sec = std::chrono::duration<double>(now - m_fail_time).count();
                if (elapsed_sec > 1.5) {
                    m_auth_failed = false;
                    set_timer_interval_ms(1000); // Revert to 1s ticks
                }
                redraw_all();
            } else {
                redraw_all();
            }
        }

        // Signal Handling
        if (signal_idx >= 0 && (pfd[signal_idx].revents & POLLIN)) {
            struct signalfd_siginfo fdsi;
            ssize_t s = read(m_signal_fd, &fdsi, sizeof(fdsi));
            if (s == sizeof(fdsi)) {
                if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGTERM) {
                    m_running = false;
                    break;
                }
            }
        }

        // Inotify config changes
        if (inotify_idx >= 0 && (pfd[inotify_idx].revents & POLLIN)) {
            handle_inotify();
        }
    }
}

void LockApp::quit() {
    m_running = false;
}

void LockApp::redraw_all() {
    for (auto& s : m_surfaces) {
        s->render();
    }
}

void LockApp::trigger_auth() {
    if (m_password.empty() || is_verifying()) return;

    m_auth->authenticate_async(m_password, [this](bool success) {
        if (success) {
            std::cout << "[miqulock] Authentication successful! Unlocking session." << std::endl;
            clear_password();
            ext_session_lock_v1_unlock_and_destroy(m_lock);
            m_lock = nullptr;
            m_running = false;
            wl_display_flush(m_display);
        } else {
            std::cout << "[miqulock] Authentication failed." << std::endl;
            clear_password();
            m_auth_failed = true;
            m_fail_time = std::chrono::steady_clock::now();
            set_timer_interval_ms(16); // 60fps for shake animation
            redraw_all();
        }
    });
    redraw_all();
}

void LockApp::on_key(uint32_t keycode, uint32_t state) {
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !m_xkb_state) return;
    if (is_verifying()) return;

    xkb_keysym_t sym = xkb_state_key_get_one_sym(m_xkb_state, keycode + 8);

    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        trigger_auth();
    } else if (sym == XKB_KEY_BackSpace) {
        if (!m_password.empty()) {
            m_password.pop_back();
            m_auth_failed = false;
            redraw_all();
        }
    } else if (sym == XKB_KEY_Escape) {
        clear_password();
        m_auth_failed = false;
        redraw_all();
    } else {
        char buf[32];
        int len = xkb_state_key_get_utf8(m_xkb_state, keycode + 8, buf, sizeof(buf));
        if (len > 0 && static_cast<unsigned char>(buf[0]) >= 32) {
            m_password.append(buf, len);
            m_auth_failed = false;
            redraw_all();
        }
    }
}

} // namespace miqulock
