#include "lock_app.hpp"
#include "auth.hpp"
#include "config.hpp"
#include <iostream>
#include <ctime>
#include <chrono>
#include <cctype>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace miqulock {

LockApp::LockApp() {
    m_auth = std::make_unique<AuthManager>();
}

LockApp::~LockApp() {
    quit();
}

bool LockApp::init() {
    m_engine = miqu::AppEngine::create();
    if (!m_engine) {
        std::cerr << "[miqulock] Failed to initialize AppEngine. Is Wayland running?\n";
        return false;
    }

    m_engine->set_quit_on_last_window_closed(false);

    bool locked = false;
    bool request_sent = m_engine->lock_session([this, &locked](bool success) {
        if (!success) {
            std::cerr << "[miqulock] Session lock request denied by compositor!\n";
            m_engine->quit(1);
            return;
        }
        std::cout << "[miqulock] Session locked by compositor.\n";
        locked = true;
    });

    if (!request_sent) {
        std::cerr << "[miqulock] Failed to request session lock.\n";
        return false;
    }

    setup_lock_screens();

    miqu::OutputManager::get()->on_outputs_changed([this]() {
        if (m_running && m_engine && m_engine->is_session_locked()) {
            m_engine->post([this]() {
                sync_lock_screens();
            });
        }
    });

    // Roundtrip to process the configure and lock events
    while (!locked && m_engine->is_session_locked()) {
        if (wl_display_dispatch(m_engine->get_display()) < 0) {
            return false;
        }
    }

    m_running = true;

    // Start 1-second clock tick thread
    m_timer_thread = std::thread([this]() {
        while (m_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!m_running) break;
            if (m_engine) {
                m_engine->post([this]() {
                    update_time_strings();
                });
            }
        }
    });

    return true;
}

std::shared_ptr<miqu::View> LockApp::create_lock_view(std::shared_ptr<ScreenLockInstance> instance) {
    const auto& cfg = Config::get();
    const auto& bg_color = cfg.get_background_color();
    const auto& primary_color = cfg.get_primary_color();
    const auto& on_primary_color = cfg.get_on_primary_color();
    const auto& surface_color = cfg.get_surface_color();
    const auto& on_surface_color = cfg.get_on_surface_color();
    const auto& outline_color = cfg.get_outline_color();
    const auto& error_color = cfg.get_error_color();

    // 1. Clock & Date (prominent typography & spacious margins)
    instance->time_view = miqu::TextViewBuilder::create()
        ->text("00:00")
        ->fontFamily(cfg.get_font_family())
        ->textSize(72)
        ->bold(true)
        ->textColor(on_surface_color)
        ->textAlignment(miqu::TextAlignment::Center)
        ->margin(0, 0, 0, 4)
        ->build();

    instance->date_view = miqu::TextViewBuilder::create()
        ->text("Loading date...")
        ->fontFamily(cfg.get_font_family())
        ->textSize(15)
        ->textColor(on_surface_color.with_alpha(0.70f))
        ->textAlignment(miqu::TextAlignment::Center)
        ->margin(0, 0, 0, 28)
        ->build();

    // 2. User Avatar & Details
    std::string username = m_auth->get_current_username();
    std::string initial = username.empty() ? "U" : username.substr(0, 1);
    for (auto& c : initial) c = static_cast<char>(std::toupper(c));

    auto initial_text = miqu::TextViewBuilder::create()
        ->text(initial)
        ->fontFamily(cfg.get_font_family())
        ->textSize(24)
        ->bold(true)
        ->textColor(primary_color)
        ->textAlignment(miqu::TextAlignment::Center)
        ->build();

    auto avatar_badge = miqu::CardViewBuilder::create()
        ->backgroundColor(primary_color.with_alpha(0.12f))
        ->stroke(2, primary_color.with_alpha(0.75f))
        ->cornerRadius(30)
        ->padding(0)
        ->addView(initial_text, miqu::LayoutParams(60, 60, miqu::Gravity::Center))
        ->margin(0, 0, 0, 12)
        ->build();

    auto username_view = miqu::TextViewBuilder::create()
        ->text(username)
        ->fontFamily(cfg.get_font_family())
        ->textSize(17)
        ->bold(true)
        ->textColor(on_surface_color)
        ->textAlignment(miqu::TextAlignment::Center)
        ->margin(0, 0, 0, 2)
        ->build();

    auto subtitle_view = miqu::TextViewBuilder::create()
        ->text("Session Locked")
        ->fontFamily(cfg.get_font_family())
        ->textSize(12)
        ->textColor(on_surface_color.with_alpha(0.50f))
        ->textAlignment(miqu::TextAlignment::Center)
        ->margin(0, 0, 0, 20)
        ->build();

    // 3. Password Input Row
    instance->password_input = miqu::EditTextBuilder::create()
        ->hint("Password...")
        ->passwordMode(true)
        ->padding(16, 12)
        ->onSubmit([this](const std::string& pwd) {
            verify_password(pwd);
        })
        ->build();
    instance->password_input->set_focused(true);

    auto submit_btn = miqu::ButtonBuilder::create()
        ->text("➔")
        ->bold(true)
        ->textSize(16)
        ->cornerRadius(22)
        ->padding(0)
        ->onClick([this, instance]() {
            if (instance && instance->password_input) {
                verify_password(instance->password_input->get_text());
            }
        })
        ->build();
    submit_btn->set_custom_colors(primary_color, on_primary_color);

    auto input_row = miqu::LinearLayoutBuilder::create()
        ->orientation(miqu::Orientation::Horizontal)
        ->gravity(miqu::Gravity::CenterVertical)
        ->spacing(8)
        ->addView(instance->password_input, miqu::LayoutParams(1.0f))
        ->addView(submit_btn, miqu::LayoutParams(44, 44))
        ->margin(0, 0, 0, 8)
        ->build();

    // 4. Caps Lock Pill
    instance->caps_view = miqu::TextViewBuilder::create()
        ->text("⇪ CAPS LOCK IS ON")
        ->fontFamily(cfg.get_font_family())
        ->textSize(11)
        ->bold(true)
        ->textColor(error_color)
        ->textAlignment(miqu::TextAlignment::Center)
        ->margin(0, 2, 0, 6)
        ->build();
    instance->caps_view->set_visibility(m_caps_lock_on ? miqu::Visibility::Visible : miqu::Visibility::Gone);

    // 5. Status / Hint View
    instance->status_view = miqu::TextViewBuilder::create()
        ->text("Press Enter to unlock")
        ->fontFamily(cfg.get_font_family())
        ->textSize(13)
        ->textColor(on_surface_color.with_alpha(0.55f))
        ->textAlignment(miqu::TextAlignment::Center)
        ->margin(0, 2, 0, 0)
        ->build();

    // 6. Auth Column inside Card (using WrapContent throughout to prevent any clipping)
    auto auth_column = miqu::LinearLayoutBuilder::create()
        ->orientation(miqu::Orientation::Vertical)
        ->gravity(miqu::Gravity::CenterHorizontal)
        ->addView(avatar_badge, miqu::LayoutParams(60, 60, miqu::Gravity::CenterHorizontal))
        ->addView(username_view, miqu::LayoutParams(static_cast<int>(miqu::LayoutDimension::MatchParent), static_cast<int>(miqu::LayoutDimension::WrapContent), miqu::Gravity::CenterHorizontal))
        ->addView(subtitle_view, miqu::LayoutParams(static_cast<int>(miqu::LayoutDimension::MatchParent), static_cast<int>(miqu::LayoutDimension::WrapContent), miqu::Gravity::CenterHorizontal))
        ->addView(input_row, miqu::LayoutParams(static_cast<int>(miqu::LayoutDimension::MatchParent), 46))
        ->addView(instance->caps_view, miqu::LayoutParams(static_cast<int>(miqu::LayoutDimension::MatchParent), static_cast<int>(miqu::LayoutDimension::WrapContent), miqu::Gravity::CenterHorizontal))
        ->addView(instance->status_view, miqu::LayoutParams(static_cast<int>(miqu::LayoutDimension::MatchParent), static_cast<int>(miqu::LayoutDimension::WrapContent), miqu::Gravity::CenterHorizontal))
        ->build();

    // 7. Elevated Floating Auth Card
    auto auth_card = miqu::CardViewBuilder::create()
        ->backgroundColor(surface_color.with_alpha(0.90f))
        ->stroke(1, outline_color.with_alpha(0.35f))
        ->cornerRadius(cfg.get_corner_radius())
        ->padding(30, 26, 30, 22)
        ->addView(auth_column, miqu::LayoutParams(static_cast<int>(miqu::LayoutDimension::MatchParent), static_cast<int>(miqu::LayoutDimension::WrapContent)))
        ->build();

    // 8. Center Column (Clock + Date + Auth Card)
    auto center_column = miqu::LinearLayoutBuilder::create()
        ->orientation(miqu::Orientation::Vertical)
        ->gravity(miqu::Gravity::CenterHorizontal)
        ->addView(instance->time_view, miqu::LayoutParams(static_cast<int>(miqu::LayoutDimension::MatchParent), static_cast<int>(miqu::LayoutDimension::WrapContent), miqu::Gravity::CenterHorizontal))
        ->addView(instance->date_view, miqu::LayoutParams(static_cast<int>(miqu::LayoutDimension::MatchParent), static_cast<int>(miqu::LayoutDimension::WrapContent), miqu::Gravity::CenterHorizontal))
        ->addView(auth_card, miqu::LayoutParams(380, static_cast<int>(miqu::LayoutDimension::WrapContent), miqu::Gravity::CenterHorizontal))
        ->build();

    // 9. Root FrameLayout covering screen
    auto root_frame = miqu::FrameLayoutBuilder::create()
        ->backgroundColor(bg_color)
        ->build();

    // 10. Power Actions Bar (Top-Right)
    if (cfg.show_power_actions()) {
        auto power_bar = miqu::LinearLayoutBuilder::create()
            ->orientation(miqu::Orientation::Horizontal)
            ->gravity(miqu::Gravity::CenterVertical)
            ->spacing(8)
            ->margin(0, 24, 28, 0)
            ->build();

        auto sleep_btn = miqu::ButtonBuilder::create()
            ->text("💤")
            ->flat(true)
            ->padding(10, 6)
            ->textSize(14)
            ->cornerRadius(14)
            ->onClick([this]() { handle_power_action("suspend"); })
            ->build();

        auto reboot_btn = miqu::ButtonBuilder::create()
            ->text("🔄")
            ->flat(true)
            ->padding(10, 6)
            ->textSize(14)
            ->cornerRadius(14)
            ->onClick([this]() { handle_power_action("reboot"); })
            ->build();

        auto shutdown_btn = miqu::ButtonBuilder::create()
            ->text("⏻")
            ->flat(true)
            ->padding(10, 6)
            ->textSize(14)
            ->cornerRadius(14)
            ->onClick([this]() { handle_power_action("poweroff"); })
            ->build();

        power_bar->add_view(sleep_btn);
        power_bar->add_view(reboot_btn);
        power_bar->add_view(shutdown_btn);

        root_frame->add_view(power_bar, miqu::LayoutParams(
            static_cast<int>(miqu::LayoutDimension::WrapContent),
            static_cast<int>(miqu::LayoutDimension::WrapContent),
            miqu::Gravity::Right | miqu::Gravity::Top
        ));
    }

    // Add Center Column to root frame
    root_frame->add_view(center_column, miqu::LayoutParams(
        380,
        static_cast<int>(miqu::LayoutDimension::WrapContent),
        miqu::Gravity::Center
    ));

    root_frame->set_on_click_listener([instance]() {
        if (instance && instance->password_input) {
            instance->password_input->set_focused(true);
            if (instance->window) instance->window->schedule_redraw();
        }
    });

    return root_frame;
}

void LockApp::handle_power_action(const std::string& action) {
    if (action == "suspend") {
        if (fork() == 0) {
            execlp("systemctl", "systemctl", "suspend", nullptr);
            _exit(1);
        }
    } else if (action == "reboot") {
        if (fork() == 0) {
            execlp("systemctl", "systemctl", "reboot", nullptr);
            _exit(1);
        }
    } else if (action == "poweroff") {
        if (fork() == 0) {
            execlp("systemctl", "systemctl", "poweroff", nullptr);
            _exit(1);
        }
    }
}

void LockApp::setup_lock_screens() {
    sync_lock_screens();
}

void LockApp::sync_lock_screens() {
    auto outputs = miqu::OutputManager::get()->get_outputs();

    // 1. Prune disconnected outputs
    auto it = m_screens.begin();
    while (it != m_screens.end()) {
        auto& screen = *it;
        bool still_present = false;
        for (const auto& out : outputs) {
            if (out.wl_output == screen->output) {
                still_present = true;
                break;
            }
        }
        if (!still_present) {
            if (screen->window) {
                screen->window->close();
            }
            it = m_screens.erase(it);
        } else {
            ++it;
        }
    }

    // 2. Spawn lock screen instance for any newly connected output
    for (const auto& out : outputs) {
        bool already_present = false;
        for (const auto& screen : m_screens) {
            if (screen->output == out.wl_output) {
                already_present = true;
                break;
            }
        }
        if (already_present) continue;

        auto instance = std::make_shared<ScreenLockInstance>();
        instance->output = out.wl_output;
        auto view = create_lock_view(instance);

        instance->window = miqu::WindowBuilder::create()
            ->role(miqu::WindowRole::SessionLock)
            ->output(out.wl_output)
            ->contentView(view)
            ->keyboardInteractive(true)
            ->onKey([this, instance](const miqu::KeyPressEvent& event) {
                if (event.pressed) {
                    bool caps = (event.modifiers & static_cast<uint32_t>(miqu::KeyboardModifier::Caps)) != 0;
                    update_caps_lock_state(caps);

                    if (event.keysym == XKB_KEY_Escape) {
                        if (instance->password_input) {
                            instance->password_input->clear();
                            if (instance->window) instance->window->schedule_redraw();
                        }
                    } else if (instance->password_input && !instance->password_input->is_focused()) {
                        instance->password_input->set_focused(true);
                        if (instance->window) instance->window->schedule_redraw();
                    }
                }
            })
            ->build();

        if (instance->window) {
            m_screens.push_back(instance);
        }
    }

    update_time_strings();
}

void LockApp::update_caps_lock_state(bool caps_on) {
    if (m_caps_lock_on == caps_on) return;
    m_caps_lock_on = caps_on;

    for (auto& s : m_screens) {
        if (!s) continue;
        if (s->caps_view) {
            s->caps_view->set_visibility(m_caps_lock_on ? miqu::Visibility::Visible : miqu::Visibility::Gone);
        }
        if (s->window) {
            s->window->schedule_redraw();
        }
    }
}

void LockApp::update_time_strings() {
    const auto& cfg = Config::get();
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::localtime(&t);
    if (!tm) return;

    char time_buf[64];
    char date_buf[128];
    std::strftime(time_buf, sizeof(time_buf), cfg.get_time_format().c_str(), tm);
    std::strftime(date_buf, sizeof(date_buf), cfg.get_date_format().c_str(), tm);

    for (auto& s : m_screens) {
        if (!s) continue;
        if (s->time_view) s->time_view->set_text(time_buf);
        if (s->date_view) s->date_view->set_text(date_buf);
        if (s->window) s->window->schedule_redraw();
    }
}

void LockApp::verify_password(const std::string& password) {
    if (m_auth->is_authenticating()) return;

    const auto& cfg = Config::get();
    const auto& primary_color = cfg.get_primary_color();
    const auto& error_color = cfg.get_error_color();

    for (auto& s : m_screens) {
        if (!s) continue;
        if (s->status_view) {
            s->status_view->set_text_color(primary_color);
            s->status_view->set_text("Authenticating...");
        }
        if (s->window) s->window->schedule_redraw();
    }

    m_auth->authenticate_async(password, [this, error_color](bool success) {
        if (!m_engine) return;
        m_engine->post([this, success, error_color]() {
            if (success) {
                std::cout << "[miqulock] Authentication succeeded, unlocking session.\n";
                m_engine->unlock_session();
                quit();
            } else {
                std::cout << "[miqulock] Authentication failed.\n";
                for (auto& s : m_screens) {
                    if (!s) continue;
                    if (s->status_view) {
                        s->status_view->set_text_color(error_color);
                        s->status_view->set_text("Authentication failed. Please try again.");
                    }
                    if (s->password_input) {
                        s->password_input->clear();
                        s->password_input->set_focused(true);
                    }
                    if (s->window) s->window->schedule_redraw();
                }
            }
        });
    });
}

void LockApp::quit() {
    if (!m_running) return;
    m_running = false;

    if (m_timer_thread.joinable()) {
        m_timer_thread.join();
    }

    for (auto& s : m_screens) {
        if (s && s->window) {
            s->window->close();
        }
    }
    m_screens.clear();

    if (m_engine) {
        m_engine->quit(0);
    }
}

void LockApp::run() {
    if (!m_engine) return;
    m_engine->enter_loop();
}

} // namespace miqulock
