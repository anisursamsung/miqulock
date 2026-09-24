#include "lock_app.hpp"
#include "config.hpp"
#include <iostream>
#include <string>
#include <unistd.h>
#include <cstring>
#include <cerrno>

using namespace miqulock;

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n\n"
              << "A modern Wayland session lock utility for the Miquland desktop.\n\n"
              << "Options:\n"
              << "  -h, --help            Show this help message and exit\n"
              << "  -v, --version         Show version information\n"
              << "  -f, --fork-on-lock, --daemonize\n"
              << "                        Detach and exit once session is securely locked\n"
              << "  -c, -C, --config PATH Path to custom configuration file\n\n"
              << "Configuration is automatically loaded from ~/.config/miqulock/miqulock.conf\n"
              << "and reloaded live when edited.\n";
}

int main(int argc, char* argv[]) {
    std::string config_path;
    bool fork_on_lock = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--init-config") {
            std::string res = miqu::Config::init_user_config("miqulock", "miqulock.conf");
            if (!res.empty()) {
                std::cout << "[miqulock] Configuration initialized at: " << res << "\n";
            } else {
                std::cout << "[miqulock] Configuration file already exists or could not be created.\n";
            }
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "miqulock 0.1.0\n";
            return 0;
        } else if (arg == "-f" || arg == "--fork-on-lock" || arg == "--daemonize") {
            fork_on_lock = true;
        } else if ((arg == "-c" || arg == "-C" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    int ready_pipe[2] = { -1, -1 };
    if (fork_on_lock) {
        if (pipe(ready_pipe) < 0) {
            std::cerr << "[miqulock] Failed to create synchronization pipe: " << strerror(errno) << "\n";
            return 1;
        }

        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "[miqulock] Failed to fork background process: " << strerror(errno) << "\n";
            close(ready_pipe[0]);
            close(ready_pipe[1]);
            return 1;
        }

        if (pid > 0) {
            // Parent process: close write end and wait for child to confirm locked
            close(ready_pipe[1]);
            char status = 1;
            ssize_t n = read(ready_pipe[0], &status, 1);
            close(ready_pipe[0]);
            if (n == 1 && status == 0) {
                // Child successfully confirmed that session is securely locked
                _exit(0);
            }
            _exit(1);
        }

        // Child process: create new session and close read end
        setsid();
        close(ready_pipe[0]);
    }

    LockApp app;
    LockApp::InitResult res = app.init(config_path);

    if (res == LockApp::InitResult::AlreadyLocked) {
        if (ready_pipe[1] >= 0) {
            char status = 0;
            ssize_t s = write(ready_pipe[1], &status, 1);
            (void)s;
            close(ready_pipe[1]);
            ready_pipe[1] = -1;
        }
        std::cout << "[miqulock] Session is already locked by another process.\n";
        return 0;
    }

    if (res != LockApp::InitResult::Success) {
        if (ready_pipe[1] >= 0) {
            char status = 1;
            ssize_t s = write(ready_pipe[1], &status, 1);
            (void)s;
            close(ready_pipe[1]);
            ready_pipe[1] = -1;
        }
        std::cerr << "[miqulock] Initialization failed." << std::endl;
        return 1;
    }

    // Session is locked! Signal parent process to exit
    if (ready_pipe[1] >= 0) {
        char status = 0;
        ssize_t s = write(ready_pipe[1], &status, 1);
        (void)s;
        close(ready_pipe[1]);
        ready_pipe[1] = -1;
    }

    std::cout << "[miqulock] Running session locker..." << std::endl;
    app.run();

    return 0;
}
