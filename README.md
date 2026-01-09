# HyFocus

A Hyprland plugin that implements Pomodoro-style focus enforcement with workspace locking and app blocking.

![HyFocus Demo](assets/demo.gif)

## Overview

HyFocus helps you stay productive by restricting workspace access and blocking app launches during focus sessions. When a timer is running, attempting to switch to non-whitelisted workspaces or launching new apps triggers a visual "shake" animation and blocks the action.

## Features

- **Pomodoro Timer**: Configurable work/break intervals (default: 25/5 minutes)
- **Workspace Locking**: Whitelist specific workspaces during focus sessions
- **App Blocking**: Prevent launching new applications during focus sessions
- **Exit Challenge**: Math problem to discourage stopping sessions early
- **Visual Feedback**: Window shake animation when attempting restricted actions
- **EWW Widgets**: Beautiful, native-feeling UI widgets (optional, customizable)
- **Pause/Resume**: Pause your session without losing progress
- **Flexible Configuration**: All settings configurable via `hyprland.conf`

## Installation

### Using hyprpm (Recommended)

```bash
hyprpm add https://github.com/ernestoCruz05/hyfocus
hyprpm enable hyfocus
```

### Manual Build

```bash
git clone https://github.com/ernestoCruz05/hyfocus
cd hyfocus

# Using CMake
cmake -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
sudo cp build/libhyfocus.so /usr/lib/hyprland/plugins/

# Or using Meson
meson setup build --buildtype=release
ninja -C build
sudo cp build/libhyfocus.so /usr/lib/hyprland/plugins/
```

## EWW Widgets (Optional)

HyFocus includes beautiful, minimal EWW widgets for a native-feeling experience. These are **completely optional** - you can use the plugin with just keybinds and Hyprland notifications, or create your own custom widgets.

### Included Widgets

| Widget | Description |
|--------|-------------|
| **Start Panel** | Select workspaces and duration, then start a session |
| **Status** | Floating timer showing remaining time (auto-shows when session active) |
| **Challenge** | Math problem widget when stopping a session early |
| **Flash** | Quick "Stay focused" overlay when attempting blocked actions |
| **Backdrop** | Fullscreen blur behind modal widgets |

### Small showcase video

https://github.com/user-attachments/assets/a09f5c13-9992-4348-a09c-50d9ba2f6144



### Setting Up EWW Widgets

1. **Install EWW** if you haven't already:
   ```bash
   # Arch
   yay -S eww
   
   # From source
   cargo install eww
   ```

2. **Copy the EWW config** to your preferred location:
   ```bash
   cp -r /path/to/hyfocus/eww ~/.config/hyfocus-eww
   ```

3. **Add layerrules** to your `hyprland.conf` for blur effects:
   ```bash
   # HyFocus EWW blur (Hyprland 0.53+)
   layerrule = blur on, match:namespace hyfocus-flash
   layerrule = blur on, match:namespace hyfocus-start
   layerrule = blur on, match:namespace hyfocus-challenge
   layerrule = blur on, match:namespace hyfocus-backdrop
   layerrule = blur on, match:namespace hyfocus-status
   
   layerrule = ignore_alpha 0.3, match:namespace hyfocus-flash
   layerrule = ignore_alpha 0.3, match:namespace hyfocus-start
   layerrule = ignore_alpha 0.3, match:namespace hyfocus-challenge
   layerrule = ignore_alpha 0.3, match:namespace hyfocus-status
   ```

4. **Enable EWW integration** in the plugin config:
   ```bash
   plugin {
       hyfocus {
           use_eww_notifications = 1
           eww_config_path = /home/YOU/.config/hyfocus-eww
       }
   }
   ```

5. **Start the EWW daemon** (add to autostart):
   ```bash
   exec-once = eww daemon -c ~/.config/hyfocus-eww
   ```

6. **Add keybinds**:
   ```bash
   # Open start panel (select workspaces & duration with UI)
   bind = SUPER, F, exec, ~/.config/hyfocus-eww/scripts/open-start
   
   # Stop session (opens challenge widget if enabled)
   bind = SUPER CTRL, F, exec, ~/.config/hyfocus-eww/scripts/show-challenge
   
   # Quick start on current workspace (no UI, uses Hyprland notifications)
   bind = SUPER SHIFT, F, hyfocus:start,
   
   # Force stop (bypasses challenge, no UI)
   bind = SUPER CTRL SHIFT, F, hyfocus:stop, force
   ```

### Customizing Widgets

The EWW widgets use SCSS for styling. Key files:

```
eww/
├── eww.yuck              # Main config, includes all widgets
├── eww.scss              # Main stylesheet
├── styles/
│   ├── _variables.scss   # Colors, fonts, spacing
│   ├── _glass.scss       # Glass/blur styling
│   ├── _start-panel.scss # Start panel styles
│   ├── _challenge.scss   # Challenge widget styles
│   └── _flash.scss       # Flash overlay styles
├── widgets/              # Widget definitions
├── windows/              # Window definitions
└── scripts/              # Control scripts
```

Feel free to modify the styles, colors, and layout to match your desktop!

## Configuration

Add to your `hyprland.conf`:

```bash
# Load the plugin
plugin = /path/to/libhyfocus.so

# Configure settings
plugin {
    hyfocus {
        # Timer settings (in minutes)
        # Break time is auto-calculated: work_time / 5
        # Example: 25 min work -> 5 min break (standard Pomodoro)
        #          50 min work -> 10 min break

        # Enforcement behavior
        enforce_during_break = false  # Allow any workspace during breaks
        
        # App blocking (EXPERIMENTAL - see note below)
        block_spawn = false           # Block launching new apps during focus
        spawn_whitelist = kitty,alacritty  # Apps allowed to launch (comma-separated)

        # Visual feedback
        shake_intensity = 15          # Pixels to shake (1-100)
        shake_duration = 300          # Animation duration in ms
        shake_frequency = 50          # Oscillation speed in ms

        # Window classes exempt from enforcement
        exception_classes = eww,rofi,wofi,dmenu,ulauncher
        
        # Exit challenge - makes stopping annoying (optional)
        # 0 = disabled, 1 = type phrase, 2 = math problem, 3 = countdown
        exit_challenge_type = 0
        exit_challenge_phrase = "I want to stop focusing"
    }
}
```

### Experimental Features

**App Blocking (`block_spawn`)**: This feature is experimental and the whitelist is not fully functional yet. When enabled, it will block ALL app launches during focus sessions, ignoring the whitelist. **Keep `block_spawn = false` unless you want complete app blocking.** This will be improved in a future release.

### Exit Challenge Types

The exit challenge adds intentional friction to prevent impulsive session stops:

| Type | Value | Description |
|------|-------|-------------|
| None | `0` | Immediate stop (default) |
| Type Phrase | `1` | Must type a specific phrase |
| Math Problem | `2` | Must solve a random math problem |
| Countdown | `3` | Must confirm 3 times to stop |

### Keybinds

#### With EWW Widgets (Recommended)

```bash
# Open start panel UI
bind = SUPER, F, exec, ~/.config/hyfocus-eww/scripts/open-start

# Stop session (opens challenge UI if enabled)
bind = SUPER CTRL, F, exec, ~/.config/hyfocus-eww/scripts/show-challenge

# Force stop (bypasses challenge)
bind = SUPER CTRL SHIFT, F, hyfocus:stop, force

# Pause/resume session
bind = SUPER, P, hyfocus:pause,
bind = SUPER SHIFT, P, hyfocus:resume,
```

#### Without EWW (Dispatcher Only)

```bash
# Start focus session on workspaces 3 and 5
bind = SUPER, F, hyfocus:start, 3,5

# Toggle focus mode (start/stop)
bind = SUPER SHIFT, F, hyfocus:toggle, 3,5

# Stop current session (uses Hyprland notification for challenge)
bind = SUPER CTRL, F, hyfocus:stop,

# Confirm exit challenge answer (when using notifications)
bind = SUPER, C, hyfocus:confirm, <answer>

# Pause/resume session
bind = SUPER, P, hyfocus:pause,
bind = SUPER SHIFT, P, hyfocus:resume,

# Show status (Hyprland notification)
bind = SUPER, S, hyfocus:status,

# Dynamically add/remove workspaces
bind = SUPER, A, hyfocus:allow, 2
bind = SUPER, D, hyfocus:disallow, 2

# Add exception class
bind = SUPER, E, hyfocus:except, firefox

# App spawn whitelist management
bind = SUPER ALT, A, hyfocus:allowapp, firefox
bind = SUPER ALT, D, hyfocus:disallowapp, firefox
```

## Commands

| Command | Arguments | Description |
|---------|-----------|-------------|
| `hyfocus:start` | `<workspace_ids>` | Start a focus session with allowed workspaces (comma-separated) |
| `hyfocus:stop` | - | Stop the current session (may require challenge) |
| `hyfocus:confirm` | `<answer>` | Submit answer for exit challenge |
| `hyfocus:pause` | - | Pause the timer |
| `hyfocus:resume` | - | Resume a paused session |
| `hyfocus:toggle` | `<workspace_ids>` | Toggle session on/off |
| `hyfocus:allow` | `<workspace_id>` | Add workspace to allowed list |
| `hyfocus:disallow` | `<workspace_id>` | Remove workspace from allowed list |
| `hyfocus:except` | `<window_class>` | Add window class to exception list |
| `hyfocus:allowapp` | `<app_name>` | Add app to spawn whitelist |
| `hyfocus:disallowapp` | `<app_name>` | Remove app from spawn whitelist |
| `hyfocus:status` | - | Display current session status |

### Using hyprctl

```bash
# Start a session
hyprctl dispatch hyfocus:start 3,5

# Check status
hyprctl dispatch hyfocus:status

# Stop session
hyprctl dispatch hyfocus:stop
```

## How It Works

### Timer System

The `FocusTimer` class manages Pomodoro-style intervals:

1. **Work Phase**: Full workspace enforcement active (user-specified duration)
2. **Break Phase**: Auto-calculated (work_time / 5), e.g., 25 min → 5 min break
3. **Visual Cues**: "Take a break!" flash when break starts, "Back to work!" when resuming

### Workspace Enforcement

The `WorkspaceEnforcer` intercepts workspace change attempts:

```
User Action → changeworkspace hook → Check allowed list → Allow/Block
                                                              ↓
                                               Block? → Shake window
```

### Window Shake Animation

When a switch is blocked, the `WindowShake` class provides visual feedback:

```
                    Window Position
                          |
    Original ─────────────┼─────────────
                         /│\
                        / │ \
                       /  │  \    ← Sinusoidal oscillation
                      /   │   \      with decay
                     /    │    \
    ───────────────•─────────────•───────────── Time
                   0ms          300ms
```

The animation uses a decaying sinusoid: `offset = intensity × sin(2π × t/period) × (1 - t/duration)`

## Architecture

```
src/
├── main.cpp              # Plugin entry point, config loading
├── globals.hpp           # Shared state and declarations
├── log.hpp               # Logging utilities
├── dispatchers.cpp/hpp   # User-facing commands
├── eventhooks.cpp/hpp    # Workspace/spawn change interception
├── FocusTimer.cpp/hpp    # Timer logic with work/break cycles
├── WorkspaceEnforcer.cpp/hpp  # Workspace validation
├── WindowShake.cpp/hpp   # Visual feedback animation
└── ExitChallenge.cpp/hpp # Exit minigame system
```

## Thread Safety

The plugin uses atomic operations and mutexes for thread safety:

- Timer runs on a background thread
- Shake animation runs on a background thread  
- All shared state is protected by atomics or mutexes

## Troubleshooting

### Plugin not loading
- Check Hyprland logs: `hyprctl log | grep hyfocus`
- Ensure the `.so` file path is correct
- Verify Hyprland version compatibility

### Workspace switches not being blocked
- Check if a session is active: `hyprctl dispatch hyfocus:status`
- Verify allowed workspaces are set correctly
- Check logs for "Blocked switch" messages

### Shake animation not working
- Ensure shake_intensity > 0
- Check if the focused window exists

### Apps still launching during focus
- Verify `block_spawn = true` in config
- **Note**: The `spawn_whitelist` is not fully implemented yet. When `block_spawn = true`, ALL apps will be blocked regardless of whitelist.
- Look for "Blocked spawn" in logs

### Can't stop session
- If exit challenge is enabled, use `hyfocus:confirm <answer>`
- For phrase challenge: type the exact phrase (case-insensitive)
- For math: submit just the number
- For countdown: keep confirming with "yes"

## Contributing

Contributions welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Submit a pull request

## License

MIT License - see [LICENSE](LICENSE) file.

## Author

**faky** - [github.com/ernestoCruz05](https://github.com/ernestoCruz05)

## Acknowledgments

- Inspired by [hycov](https://github.com/DreamMaoMao/hycov) plugin architecture
- Pomodoro Technique by Francesco Cirillo
