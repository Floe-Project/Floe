<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Requirements

Floe is an audio plugin for Windows and macOS. It's available in the following audio plugin formats: [CLAP](https://cleveraudio.org/1-feature-overview/) (Windows and macOS), VST3 (Windows and macOS) and AU (macOS only). 

Just for clarity: there's no Linux, no 'standalone' plugin and no AAX (Pro Tools) support. We hope to expand Floe's compatibility in the future.

### Windows
- {{#include ../../mdbook_config.txt:min-windows-version}} or later
- 64-bit computer
- x86-64 processor with SSE2 support (almost all processors in a Windows PC since ~2006 have this)
- CLAP or VST3 host

### macOS
- {{#include ../../mdbook_config.txt:min-macos-version}} or later
- Apple Silicon or Intel (Floe is a universal binary)
- CLAP, AU (v2) or VST3 host

### Plugin Hosts (DAWs)
Here are some examples of plugin hosts that can run Floe. Where possible, we recommend using the CLAP version Floe.
- **CLAP** hosts on <i class="fa fa-windows"></i> Windows & <i class="fa fa-apple"></i> macOS: Reaper, Bitwig, FL Studio (2024 or newer) and Studio One Pro (v7 or newer).
- **VST3** hosts on <i class="fa fa-windows"></i> Windows & <i class="fa fa-apple"></i> macOS: Cubase, Ableton Live and countless others.
- **AU** hosts on <i class="fa fa-apple"></i> macOS include: Logic Pro, GarageBand, and more.
