#include "auth.hpp"
#include <security/pam_appl.h>
#include <unistd.h>
#include <pwd.h>
#include <cstring>
#include <cstdlib>
#include <iostream>

namespace miqulock {

struct PamAuthData {
    std::string password;
};

int AuthManager::pam_conversation(int num_msg, const struct pam_message** msg,
                                  struct pam_response** resp, void* appdata_ptr)
{
    if (num_msg <= 0 || num_msg > 32) return PAM_CONV_ERR;

    auto* data = static_cast<PamAuthData*>(appdata_ptr);
    if (!data) return PAM_CONV_ERR;

    auto* reply = static_cast<struct pam_response*>(calloc(num_msg, sizeof(struct pam_response)));
    if (!reply) return PAM_BUF_ERR;

    for (int i = 0; i < num_msg; ++i) {
        int style = msg[i]->msg_style;
        if (style == PAM_PROMPT_ECHO_OFF || style == PAM_PROMPT_ECHO_ON) {
            reply[i].resp = strdup(data->password.c_str());
            reply[i].resp_retcode = 0;
        } else {
            reply[i].resp = nullptr;
            reply[i].resp_retcode = 0;
        }
    }

    *resp = reply;
    return PAM_SUCCESS;
}

AuthManager::AuthManager() {
    const char* user = getenv("USER");
    if (!user || !*user) {
        struct passwd* pw = getpwuid(getuid());
        if (pw && pw->pw_name) {
            user = pw->pw_name;
        }
    }
    m_username = user ? user : "user";
}

AuthManager::~AuthManager() {
    if (m_auth_thread.joinable()) {
        m_auth_thread.join();
    }
}

std::string AuthManager::get_current_username() const {
    return m_username;
}

void AuthManager::authenticate_async(const std::string& password, std::function<void(bool success)> callback) {
    if (m_authenticating.exchange(true)) {
        return; // Already authenticating
    }

    if (m_auth_thread.joinable()) {
        m_auth_thread.join();
    }

    m_auth_thread = std::thread([this, password, callback]() {
        PamAuthData auth_data;
        auth_data.password = password;

        struct pam_conv conv = {
            &AuthManager::pam_conversation,
            &auth_data
        };

        pam_handle_t* pamh = nullptr;
        // Try 'login' service first, fallback to 'su' or 'system-auth'
        int ret = pam_start("login", m_username.c_str(), &conv, &pamh);
        if (ret != PAM_SUCCESS) {
            ret = pam_start("su", m_username.c_str(), &conv, &pamh);
        }

        bool success = false;
        if (ret == PAM_SUCCESS && pamh) {
            ret = pam_authenticate(pamh, 0);
            if (ret == PAM_SUCCESS) {
                ret = pam_acct_mgmt(pamh, 0);
                if (ret == PAM_SUCCESS) {
                    success = true;
                }
            }
            pam_end(pamh, ret);
        }

        m_authenticating = false;
        if (callback) {
            callback(success);
        }
    });
}

} // namespace miqulock
