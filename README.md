# miqulock

A lightweight, modern, and beautiful Material Design 3 companion screen locker for `miquland`, built with Wayland (`ext-session-lock-v1`), Cairo, and Linux PAM.

---

## Features
- **ext-session-lock-v1 Protocol**: Secure Wayland session locking with multi-monitor support.
- **Material Design 3 Theming**: Automatically synchronizes with `miquland`'s active color palette (`~/.config/miquland/miquland.conf`).
- **Smooth Animations**: Glowing password input pill, discrete bullet indicators, shake on incorrect password.
- **Asynchronous Linux PAM**: Non-blocking authentication on a worker thread.

---

## Building & Installing

### Requirements
- `wayland-client`, `wayland-protocols`
- `cairo`
- `xkbcommon`
- `libpam0g-dev`
- `cmake` (>= 3.20)
- `C++20` compiler (GCC / Clang)

### Build
```bash
./make.sh
```

### Install
```bash
./make.sh install
```
