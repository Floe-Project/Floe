<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Requirements

Floe is an audio plugin for Windows and macOS. It's available in the following audio plugin formats: [CLAP](https://cleveraudio.org/1-feature-overview/) (Windows and macOS), VST3 (Windows and macOS) and AU (macOS only). 

There's no 'standalone' version at the moment. And also no Linux version, but it's planned. Finally, there's no AAX (Pro Tools) version.

### Windows
- {{#include ../../../mdbook_config.txt:min-windows-version}} or later
- 64-bit computer
- CLAP or VST3 host

### macOS
- {{#include ../../../mdbook_config.txt:min-macos-version}} or later
- Apple Silicon or Intel (Floe is a universal binary)
- CLAP, AU (v2) or VST3 host

### Plugin Hosts (DAWs)
Examples of CLAP/VST3 hosts on Windows & macOS include: Reaper, Bitwig, Cubase, Ableton Live, Studio One, and countless others.

Examples of AU hosts on macOS include: Logic Pro, GarageBand, and more.
