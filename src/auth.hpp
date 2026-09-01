#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <security/pam_appl.h>

namespace miqulock {

class AuthManager {
public:
    AuthManager();
    ~AuthManager();

    std::string get_current_username() const;

    // Trigger async PAM authentication
    void authenticate_async(const std::string& password, std::function<void(bool success)> callback);

    bool is_authenticating() const { return m_authenticating; }

    static int pam_conversation(int num_msg, const struct pam_message** msg,
                                struct pam_response** resp, void* appdata_ptr);

private:
    std::string m_username;
    std::atomic<bool> m_authenticating{false};
    std::thread m_auth_thread;
};

} // namespace miqulock
