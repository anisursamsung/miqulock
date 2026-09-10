#pragma once

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <chrono>

struct ext_session_lock_manager_v1;
struct ext_session_lock_v1;

namespace miqulock {

class LockSurface;
class AuthManager;

class LockApp {
public:
    LockApp();
    ~LockApp();

    bool init();
    void run();
    void quit();

    void on_key(uint32_t keycode, uint32_t state);
    void redraw_all();
    void trigger_auth();

    struct wl_display* get_display() const { return m_display; }
    struct wl_compositor* get_compositor() const { return m_compositor; }
    struct wl_shm* get_shm() const { return m_shm; }
    struct ext_session_lock_v1* get_lock() const { return m_lock; }

    const std::string& get_password() const { return m_password; }
    std::string get_username() const;
    bool is_verifying() const;
    bool is_auth_failed() const { return m_auth_failed; }
    bool is_caps_lock_active() const { return m_caps_lock_active; }
    double get_shake_offset() const;
    void clear_password();

    // Internal handlers called by wayland callbacks
    void handle_global(struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
    void handle_global_remove(uint32_t name);
    void handle_seat_caps(uint32_t caps);
    void handle_keymap(uint32_t format, int32_t fd, uint32_t size);
    void handle_key_event(uint32_t key, uint32_t state);
    void handle_modifiers(uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group);

private:
    struct wl_display* m_display = nullptr;
    struct wl_registry* m_registry = nullptr;
    struct wl_compositor* m_compositor = nullptr;
    struct wl_shm* m_shm = nullptr;
    struct wl_seat* m_seat = nullptr;
    struct wl_keyboard* m_keyboard = nullptr;
    struct ext_session_lock_manager_v1* m_lock_manager = nullptr;
    struct ext_session_lock_v1* m_lock = nullptr;

    struct xkb_context* m_xkb_context = nullptr;
    struct xkb_keymap* m_xkb_keymap = nullptr;
    struct xkb_state* m_xkb_state = nullptr;

    std::vector<std::pair<uint32_t, struct wl_output*>> m_outputs;
    std::vector<std::unique_ptr<LockSurface>> m_surfaces;
    std::unique_ptr<AuthManager> m_auth;

    std::string m_password;
    bool m_running = true;
    bool m_auth_failed = false;
    bool m_caps_lock_active = false;
    std::chrono::steady_clock::time_point m_fail_time;

    int m_timer_fd = -1;
    int m_signal_fd = -1;
    int m_inotify_fd = -1;
    int m_inotify_dir_wd = -1;
    int m_inotify_file_wd = -1;

    void setup_timer();
    void set_timer_interval_ms(int ms);
    void setup_inotify();
    void cleanup_inotify();
    void handle_inotify();
    void setup_signals();
    void cleanup_signals();
};

} // namespace miqulock
