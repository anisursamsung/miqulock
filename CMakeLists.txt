cmake_minimum_required(VERSION 3.20)
project(miqulock LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib;$ORIGIN/../lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(MIQUTOOLKIT REQUIRED IMPORTED_TARGET miqutoolkit)
pkg_check_modules(PAM REQUIRED IMPORTED_TARGET pam)

add_executable(miqulock
    src/main.cpp
    src/config.cpp
    src/auth.cpp
    src/lock_app.cpp
)

target_include_directories(miqulock PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(miqulock PRIVATE
    PkgConfig::MIQUTOOLKIT
    PkgConfig::PAM
    Threads::Threads
    stdc++fs
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../miqutoolkit/build/libmiqutoolkit.so")
    target_link_libraries(miqulock PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../miqutoolkit/build/libmiqutoolkit.so")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../miqutoolkit/include")
    target_include_directories(miqulock BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../miqutoolkit/include")
endif()

target_compile_options(miqulock PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

include(GNUInstallDirs)
install(TARGETS miqulock RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES assets/miqulock.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/miqulock)
