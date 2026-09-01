#include "lock_app.hpp"
#include "config.hpp"
#include <iostream>

using namespace miqulock;

int main(int argc, char* argv[]) {
    // Load active theme and configuration
    Config::get().load();

    LockApp app;
    if (!app.init()) {
        std::cerr << "[miqulock] Initialization failed." << std::endl;
        return 1;
    }

    std::cout << "[miqulock] Running screen locker..." << std::endl;
    app.run();

    return 0;
}
