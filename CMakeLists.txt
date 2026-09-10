cmake_minimum_required(VERSION 3.20)
project(miqulock LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols>=1.24)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(PAM REQUIRED IMPORTED_TARGET pam)

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# Generate ext-session-lock protocol client files
set(SESSION_LOCK_XML "${CMAKE_CURRENT_SOURCE_DIR}/protocols/ext-session-lock-v1.xml")

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} client-header ${SESSION_LOCK_XML} ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-client-protocol.h
    DEPENDS ${SESSION_LOCK_XML}
)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-client-protocol.c
    COMMAND ${WAYLAND_SCANNER} private-code ${SESSION_LOCK_XML} ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-client-protocol.c
    DEPENDS ${SESSION_LOCK_XML}
)

add_executable(miqulock
    src/main.cpp
    src/config.cpp
    src/auth.cpp
    src/lock_surface.cpp
    src/lock_app.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-client-protocol.h
    ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-client-protocol.c
)

target_include_directories(miqulock PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(miqulock PRIVATE
    PkgConfig::WAYLAND_CLIENT
    PkgConfig::CAIRO
    PkgConfig::XKBCOMMON
    PkgConfig::PAM
    Threads::Threads
)

target_compile_options(miqulock PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

include(GNUInstallDirs)
install(TARGETS miqulock RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES assets/miqulock.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/miqulock)
