# Knipser

[![Build Status](https://github.com/karstenweikamp/knipser/actions/workflows/build.yml/badge.svg)](https://github.com/karstenweikamp/knipser/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Knipser is a lightweight, native screenshot utility built specifically for wlroots-based Wayland compositors. It provides a seamless integration with your desktop environment through a system tray icon while offering powerful screenshot capabilities.

## Features

- **Wayland Native**: Built specifically for Wayland using the wlroots protocol extensions
- **System Tray Integration**: Convenient access through a StatusNotifierItem in your system tray
- **Multi-Monitor Support**: Intelligently detects and handles multi-monitor setups
- **Timestamp Filenames**: Screenshots automatically saved with date and time information
- **Minimal Dependencies**: Minimal runtime dependencies for a lightweight footprint
- **Non-Intrusive**: Runs quietly in your system tray until needed

## Installation

### From Source

```bash
# Clone the repository
git clone https://github.com/karstenweikamp/knipser.git
cd knipser

# Create build directory
mkdir build && cd build

# Build
cmake -G Ninja ..
ninja

# Install (optional)
sudo ninja install
```

### Dependencies

- Wayland (with wlroots-based compositor)
- libpng
- systemd (for D-Bus integration)

## Usage

Launch Knipser from your application menu or terminal:

```bash
knipser
```

Once running, you'll see the Knipser icon in your system tray. Right-click on the icon to see available options:

- **Take Screenshot**: Captures your entire screen
- **Settings** (coming soon): Configure screenshot options
- **Quit** (coming soon): Exit Knipser

Screenshots are saved to your current working directory with filenames in the format `screenshot_YYYY-MM-DDThh:mm:ss.png`.

## Architecture

Knipser is designed with modularity in mind:

- **Core**: Handles high-level screenshot coordination
- **Wayland Client**: Interfaces with the compositor using wlr-screencopy-unstable-v1 protocol
- **Output Management**: Detects and coordinates multi-monitor setups via wlr-output-management-unstable-v1
- **Tray Integration**: Provides system tray presence using the StatusNotifierItem protocol

## License

This project is licensed under the MIT License - see the LICENSE file for details.

---
