#include "lock_app.hpp"
#include "config.hpp"
#include <iostream>
#include <string>

using namespace miqulock;

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n\n"
              << "A modern Wayland session lock utility for the Miquland desktop.\n\n"
              << "Options:\n"
              << "  -h, --help            Show this help message and exit\n"
              << "  -v, --version         Show version information\n"
              << "  -c, -C, --config PATH Path to custom configuration file\n\n"
              << "Configuration is automatically loaded from ~/.config/miqulock/miqulock.conf\n"
              << "and reloaded live when edited.\n";
}

int main(int argc, char* argv[]) {
    std::string config_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "miqulock 0.1.0\n";
            return 0;
        } else if ((arg == "-c" || arg == "-C" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    // Load active configuration (creates user config on first launch if absent)
    Config::get().load(config_path);

    LockApp app;
    if (!app.init()) {
        std::cerr << "[miqulock] Initialization failed." << std::endl;
        return 1;
    }

    std::cout << "[miqulock] Running session locker..." << std::endl;
    app.run();

    return 0;
}
