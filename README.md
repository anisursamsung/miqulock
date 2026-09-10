# miqulock

A modern Wayland session lock utility designed for the **Miquland** desktop ecosystem.

## Features
- **Ext Session Lock Protocol**: Uses the standard Wayland `ext-session-lock-v1` protocol to securely shield all outputs.
- **Dynamic Display Hotplugging**: Dynamically handles display connect/disconnect events while locked, immediately shielding newly attached monitors.
- **Live Inotify Reloading**: Automatically reloads colors, typography, and clock formats when `~/.config/miqulock/miqulock.conf` is edited.
- **Real-Time Clock & Smooth Animations**: Timer-driven rendering guarantees the clock updates every second without requiring user interaction, while authentication failure shake animations play smoothly at 60fps.
- **Caps Lock Detection**: Displays a clean visual badge when Caps Lock is active.
- **Secure PAM Authentication**: Non-blocking PAM validation with memory zeroing (`explicit_bzero`) for sensitive password buffers.
- **First-Launch Auto-Config**: Automatically copies default configuration to `~/.config/miqulock/miqulock.conf` on first launch.

## Installation

### Local Build
```bash
./make.sh
```

### System-wide Install
```bash
sudo ./make.sh
```

## Configuration

The configuration file is located at `~/.config/miqulock/miqulock.conf` (created automatically on first launch from system defaults).

Example configuration:
```ini
[general]
font = Sans
time_format = %H:%M
date_format = %A, %B %d
corner_radius = 24

primary = #0066ff
on_primary = #ffffff
primary_container = #cce5ff
background = #0b0f19
surface = #161f30
on_surface = #f8fafc
outline = #3b82f6
error = #ef4444
```

## Usage

```bash
# Lock the session
miqulock

# Show help
miqulock --help

# Use custom config
miqulock --config /path/to/custom.conf
```
