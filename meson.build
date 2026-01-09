project('hyfocus', 'cpp',
    version: '0.1.0',
    license: 'MIT',
    default_options: [
        'cpp_std=c++23',
        'warning_level=2',
        'buildtype=release',
    ])

cpp = meson.get_compiler('cpp')

# Find Hyprland
hyprland = dependency('hyprland')

# Build shared library
shared_library('hyfocus',
    'src/main.cpp',
    'src/dispatchers.cpp',
    'src/eventhooks.cpp',
    'src/FocusTimer.cpp',
    'src/WorkspaceEnforcer.cpp',
    'src/WindowShake.cpp',
    'src/ExitChallenge.cpp',
    dependencies: [hyprland],
    install: true,
    install_dir: join_paths(get_option('libdir'), 'hyprland', 'plugins'))
