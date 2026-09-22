#pragma once

#include <miqutoolkit/miqutoolkit.hpp>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>

namespace miqulock {

class AuthManager;

struct ScreenLockInstance {
    struct wl_output* output = nullptr;
    std::shared_ptr<miqu::Window> window;
    std::shared_ptr<miqu::TextView> time_view;
    std::shared_ptr<miqu::TextView> date_view;
    std::shared_ptr<miqu::EditText> password_input;
    std::shared_ptr<miqu::TextView> status_view;
    std::shared_ptr<miqu::TextView> caps_view;
};

class LockApp {
public:
    LockApp();
    ~LockApp();

    enum class InitResult {
        Success,
        AlreadyLocked,
        Failed
    };

    InitResult init();
    void run();
    void quit();

private:
    void setup_lock_screens();
    void sync_lock_screens();
    std::shared_ptr<miqu::View> create_lock_view(std::shared_ptr<ScreenLockInstance> instance);
    std::shared_ptr<miqu::View> create_background_view(std::shared_ptr<ScreenLockInstance> instance);
    std::shared_ptr<miqu::View> create_power_bar();
    void setup_clock_views(std::shared_ptr<ScreenLockInstance> instance);
    std::shared_ptr<miqu::View> create_auth_card(std::shared_ptr<ScreenLockInstance> instance);
    void update_time_strings();
    void update_caps_lock_state(bool caps_on);
    void verify_password(const std::string& password);
    void handle_power_action(const std::string& action);

    std::shared_ptr<miqu::AppEngine> m_engine;
    std::unique_ptr<AuthManager> m_auth;
    std::vector<std::shared_ptr<ScreenLockInstance>> m_screens;

    std::atomic<bool> m_running{false};
    std::thread m_timer_thread;
    bool m_caps_lock_on{false};
};

} // namespace miqulock
