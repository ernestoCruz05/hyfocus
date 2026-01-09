cmake_minimum_required(VERSION 3.19)
project(hyfocus
    VERSION 0.1.0
    DESCRIPTION "A Pomodoro focus enforcement plugin for Hyprland"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Hyprland
find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND REQUIRED hyprland)

# Create shared library
add_library(hyfocus SHARED
    src/main.cpp
    src/dispatchers.cpp
    src/eventhooks.cpp
    src/FocusTimer.cpp
    src/WorkspaceEnforcer.cpp
    src/WindowShake.cpp
    src/ExitChallenge.cpp
)

target_include_directories(hyfocus PRIVATE
    ${HYPRLAND_INCLUDE_DIRS}
    src/
)

target_compile_options(hyfocus PRIVATE
    ${HYPRLAND_CFLAGS_OTHER}
    -Wall
    -Wextra
    -fPIC
)

target_link_libraries(hyfocus PRIVATE
    ${HYPRLAND_LIBRARIES}
)

# Install target
install(TARGETS hyfocus LIBRARY DESTINATION lib/hyprland/plugins)
