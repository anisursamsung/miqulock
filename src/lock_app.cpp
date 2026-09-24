#include "lock_app.hpp"
#include "auth.hpp"
#include "config.hpp"
#include <iostream>
#include <ctime>
#include <chrono>
#include <cctype>
#include <algorithm>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace miqulock {

LockApp::LockApp() {
    m_auth = std::make_unique<AuthManager>();
}

LockApp::~LockApp() {
    quit();
}

LockApp::InitResult LockApp::init(const std::string& custom_config_path) {
    m_engine = miqu::AppEngine::create();
    if (!m_engine) {
        std::cerr << "[miqulock] Failed to initialize AppEngine. Is Wayland running?\n";
        return InitResult::Failed;
    }

    m_engine->set_quit_on_last_window_closed(false);

    // Rule 17: Canonical init order (AppEngine -> load overlay -> setup config watcher)
    Config::get().load(custom_config_path);
    m_engine->setup_config_watcher();

    m_engine->add_theme_change_listener([this]() {
        if (m_engine) {
            m_engine->post([this]() {
                Config::get().reload();
                reload_ui();
            });
        }
    });

    bool locked = false;
    bool request_denied = false;
    bool request_sent = m_engine->lock_session([this, &locked, &request_denied](bool success) {
        if (!success) {
            std::cerr << "[miqulock] Session lock request denied by compositor (already locked).\n";
            request_denied = true;
            m_engine->quit(0);
            return;
        }
        std::cout << "[miqulock] Session locked by compositor.\n";
        locked = true;
    });

    if (!request_sent) {
        std::cerr << "[miqulock] Failed to request session lock.\n";
        return InitResult::Failed;
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
    while (!locked && !request_denied && m_engine->is_session_locked()) {
        if (wl_display_dispatch(m_engine->get_display()) < 0) {
            return InitResult::Failed;
        }
    }

    if (request_denied) {
        return InitResult::AlreadyLocked;
    }

    if (!locked) {
        return InitResult::Failed;
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

    return InitResult::Success;
}


std::shared_ptr<miqu::View> LockApp::create_background_view(std::shared_ptr<ScreenLockInstance> instance) {
    const auto& cfg = Config::get();
    const auto& bg_path = cfg.get_wallpaper_path();
    if (bg_path.empty()) {
        return nullptr;
    }

    auto builder = miqu::ImageViewBuilder::create()
        ->source(bg_path)
        ->fitMode(miqu::FitMode::Cover)
        ->qualityMode(miqu::ImageQuality::FullOriginal);

    if (cfg.get_blur_radius() > 0) {
        builder->blurRadius(cfg.get_blur_radius());
    }
    if (cfg.get_dim_alpha() > 0.001f) {
        builder->dim(cfg.get_dim_alpha());
    }

    auto bg_view = builder->build();

    bg_view->set_on_click_listener([instance]() {
        if (instance && instance->password_input) {
            instance->password_input->set_focused(true);
            if (instance->window) instance->window->schedule_redraw();
        }
    });

    return bg_view;
}


void LockApp::setup_clock_views(std::shared_ptr<ScreenLockInstance> instance) {
    const auto& cfg = Config::get();
    const auto& on_surface_color = cfg.get_on_surface_color();

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
}

std::shared_ptr<miqu::View> LockApp::create_auth_card(std::shared_ptr<ScreenLockInstance> instance) {
    const auto& cfg = Config::get();
    const auto& primary_color = cfg.get_primary_color();
    const auto& on_primary_color = cfg.get_on_primary_color();
    const auto& surface_color = cfg.get_surface_color();
    const auto& on_surface_color = cfg.get_on_surface_color();
    const auto& outline_color = cfg.get_outline_color();
    const auto& error_color = cfg.get_error_color();

    // User Avatar Badge
    std::string username = m_auth->get_current_username();
    std::string initial = username.empty() ? "U" : username.substr(0, 1);
    for (auto& c : initial) c = static_cast<char>(std::toupper(c));

    auto initial_text = miqu::TextViewBuilder::create()
        ->text(initial)
        ->fontFamily(cfg.get_font_family())
        ->textSize(28)
        ->bold(true)
        ->textColor(primary_color)
        ->textAlignment(miqu::TextAlignment::Center)
        ->build();

    auto avatar_badge = miqu::CardViewBuilder::create()
        ->backgroundColor(primary_color.with_alpha(0.12f))
        ->stroke(2, primary_color.with_alpha(0.75f))
        ->cornerRadius(36)
        ->padding(0)
        ->addView(initial_text, miqu::LayoutParams(72, 72, miqu::Gravity::Center))
        ->margin(0, 4, 0, 16)
        ->build();

    auto username_view = miqu::TextViewBuilder::create()
        ->text(username)
        ->fontFamily(cfg.get_font_family())
        ->textSize(19)
        ->bold(true)
        ->textColor(on_surface_color)
        ->textAlignment(miqu::TextAlignment::Center)
        ->margin(0, 0, 0, 4)
        ->build();

    auto subtitle_view = miqu::TextViewBuilder::create()
        ->text("Session Locked")
        ->fontFamily(cfg.get_font_family())
        ->textSize(13)
        ->textColor(on_surface_color.with_alpha(0.50f))
        ->textAlignment(miqu::TextAlignment::Center)
        ->margin(0, 0, 0, 26)
        ->build();

    // Password input row
    instance->password_input = miqu::EditTextBuilder::create()
        ->hint("Password...")
        ->passwordMode(true)
        ->padding(18, 14)
        ->onSubmit([this](const std::string& pwd) {
            verify_password(pwd);
        })
        ->build();
    instance->password_input->set_focused(true);

    auto submit_btn = miqu::ButtonBuilder::create()
        ->text("➔")
        ->bold(true)
        ->textSize(16)
        ->cornerRadius(24)
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
        ->spacing(10)
        ->addView(instance->password_input, miqu::LayoutParams(1.0f))
        ->addView(submit_btn, miqu::LayoutParams(48, 48))
        ->margin(0, 0, 0, 14)
        ->build();

    // Caps lock indicator
    instance->caps_view = miqu::TextViewBuilder::create()
        ->text("⇪ CAPS LOCK IS ON")
        ->fontFamily(cfg.get_font_family())
        ->textSize(12)
        ->bold(true)
        ->textColor(error_color)
        ->textAlignment(miqu::TextAlignment::Center)
        ->margin(0, 2, 0, 8)
        ->build();
    instance->caps_view->set_visibility(m_caps_lock_on ? miqu::Visibility::Visible : miqu::Visibility::Gone);

    // Status / hint
    instance->status_view = miqu::TextViewBuilder::create()
        ->text("Press Enter to unlock")
        ->fontFamily(cfg.get_font_family())
        ->textSize(13)
        ->textColor(on_surface_color.with_alpha(0.55f))
        ->textAlignment(miqu::TextAlignment::Center)
        ->margin(0, 2, 0, 4)
        ->build();

    auto auth_column = miqu::LinearLayoutBuilder::create()
        ->orientation(miqu::Orientation::Vertical)
        ->gravity(miqu::Gravity::CenterHorizontal)
        ->addView(avatar_badge, miqu::LayoutParams(72, 72, miqu::Gravity::CenterHorizontal))
        ->addView(username_view, miqu::LayoutParams(static_cast<int>(miqu::LayoutDimension::MatchParent), static_cast<int>(miqu::LayoutDimension::WrapContent), miqu::Gravity::CenterHorizontal))
        ->addView(subtitle_view, miqu::LayoutParams(static_cast<int>(miqu::LayoutDimension::MatchParent), static_cast<int>(miqu::LayoutDimension::WrapContent), miqu::Gravity::CenterHorizontal))
        ->addView(input_row, miqu::LayoutParams(static_cast<int>(miqu::LayoutDimension::MatchParent), 50))
        ->addView(instance->caps_view, miqu::LayoutParams(static_cast<int>(miqu::LayoutDimension::MatchParent), static_cast<int>(miqu::LayoutDimension::WrapContent), miqu::Gravity::CenterHorizontal))
        ->addView(instance->status_view, miqu::LayoutParams(static_cast<int>(miqu::LayoutDimension::MatchParent), static_cast<int>(miqu::LayoutDimension::WrapContent), miqu::Gravity::CenterHorizontal))
        ->build();

    auto card_builder = miqu::CardViewBuilder::create()
        ->backgroundColor(surface_color.with_alpha(0.90f))
        ->stroke(1, outline_color.with_alpha(0.35f))
        ->cornerRadius(cfg.get_corner_radius())
        ->padding(42, 38, 42, 34)
        ->addView(auth_column, miqu::LayoutParams(static_cast<int>(miqu::LayoutDimension::MatchParent), static_cast<int>(miqu::LayoutDimension::WrapContent)));

    if (cfg.show_power_actions()) {
        instance->power_menu = miqu::ActionMenuBuilder::create()
            ->item("Sleep", "system-suspend-symbolic", [this]() {
                handle_power_action("suspend");
            })
            ->item("Restart", "system-reboot-symbolic", [this]() {
                handle_power_action("reboot");
            })
            ->separator()
            ->destructive("Shut Down", "system-shutdown-symbolic", [this]() {
                handle_power_action("poweroff");
            })
            ->minWidth(170)
            ->build();

        auto power_btn = miqu::ButtonBuilder::create()
            ->text("⏻")
            ->flat(true)
            ->textSize(15)
            ->padding(2, 2)
            ->cornerRadius(17)
            ->margin(0, -18, -20, 0)
            ->build();
        power_btn->set_custom_colors(miqu::Color::transparent(), on_surface_color.with_alpha(0.55f));

        power_btn->set_on_click_listener([instance, power_btn]() {
            if (instance && instance->power_menu) {
                instance->power_menu->show_as_dropdown(power_btn, miqu::PopupGravity::BottomStart);
            }
        });

        // Position in the top-right corner of the card with explicit 34x34 bounds
        card_builder->addView(power_btn, miqu::LayoutParams(
            34, 34,
            miqu::Gravity::Right | miqu::Gravity::Top
        ));
    }

    return card_builder->build();
}

std::shared_ptr<miqu::View> LockApp::create_lock_view(std::shared_ptr<ScreenLockInstance> instance) {
    const auto& cfg = Config::get();

    auto root_frame = miqu::FrameLayoutBuilder::create()
        ->backgroundColor(cfg.get_background_color().with_alpha(1.0f))
        ->build();

    // 1. Full-window Background ImageView
    auto bg_view = create_background_view(instance);
    if (bg_view) {
        root_frame->add_view(bg_view, miqu::LayoutParams(
            static_cast<int>(miqu::LayoutDimension::MatchParent),
            static_cast<int>(miqu::LayoutDimension::MatchParent)
        ));
    }

    // 2. Center Column (Clock + Date + Auth Card)
    setup_clock_views(instance);
    auto auth_card = create_auth_card(instance);

    auto center_column = miqu::LinearLayoutBuilder::create()
        ->orientation(miqu::Orientation::Vertical)
        ->gravity(miqu::Gravity::CenterHorizontal)
        ->addView(instance->time_view, miqu::LayoutParams(static_cast<int>(miqu::LayoutDimension::MatchParent), static_cast<int>(miqu::LayoutDimension::WrapContent), miqu::Gravity::CenterHorizontal))
        ->addView(instance->date_view, miqu::LayoutParams(static_cast<int>(miqu::LayoutDimension::MatchParent), static_cast<int>(miqu::LayoutDimension::WrapContent), miqu::Gravity::CenterHorizontal))
        ->addView(auth_card, miqu::LayoutParams(440, static_cast<int>(miqu::LayoutDimension::WrapContent), miqu::Gravity::CenterHorizontal))
        ->build();

    root_frame->add_view(center_column, miqu::LayoutParams(
        440,
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

void LockApp::reload_ui() {
    for (auto& screen : m_screens) {
        if (screen && screen->window) {
            auto view = create_lock_view(screen);
            screen->window->set_content_view(view);
            screen->window->schedule_redraw();
        }
    }
    update_time_strings();
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
