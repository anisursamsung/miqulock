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

    bool init();
    void run();
    void quit();

private:
    void setup_lock_screens();
    std::shared_ptr<miqu::View> create_lock_view(std::shared_ptr<ScreenLockInstance> instance);
    void update_time_strings();
    void update_caps_lock_state(bool caps_on);
    void verify_password(const std::string& password);

    std::shared_ptr<miqu::AppEngine> m_engine;
    std::unique_ptr<AuthManager> m_auth;
    std::vector<std::shared_ptr<ScreenLockInstance>> m_screens;

    std::atomic<bool> m_running{false};
    std::thread m_timer_thread;
    bool m_caps_lock_on{false};
};

} // namespace miqulock
