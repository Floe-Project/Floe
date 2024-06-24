<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

<div class="warning">
Floe is heavily under development. This manual is a work-in-progress.

Some things may be incorrect, missing or broken. We are bit-by-bit filling in the gaps ahead of version 1.0.0.

</div>

# Floe User Manual

Welcome to the user manual for Floe. This manual is designed to help you get started with Floe and to provide a reference for all of its features.

This manual contains information for Floe version {{#include ../../version.txt}} and earlier.

## What is Floe?
Floe is a sample-based synthesis plugin developed by [FrozenPlain](https://frozenplain.com). It's free and open-source and developed separately from FrozenPlain's commercial products. The project is hosted on [Github](https://github.com/Floe-Synth/Floe).

Floe requires sample libraries. These are not included with the plugin. At the moment only FrozenPlain offer paid and free sample libraries, but we are looking to rapidly expand the number of libraries available, both free and paid. You can also make your own sample libraries using our Lua sample-library format.

Key features:
- CLAP, VST3 and AU plugin formats on Windows and macOS (Linux support is planned)
- Three-layer architecture for blending timbres into new sounds
- Designed to easily create playable and expressive instruments
- Sound shaping tools: filters, envelopes, LFOs and an effects rack
- Powerful browsers for sample and preset collections
- Supports multisampled instruments: velocity layers, round-robin, crossfade layers
- Crafted with longevity and freedom in mind

## Requirements
### Windows
- {{#include ../../mdbook_config.txt:min-windows-version}} or later
- 64-bit
- CLAP or VST3 host

### macOS
- {{#include ../../mdbook_config.txt:min-macos-version}} or later
- Apple Silicon or Intel
- CLAP, AU (version 2) or VST3 host

## Previously know as Mirage
Floe is a continuation of FrozenPlain's Mirage plugin (developed from 2018 to 2023). The project has taken a new direction and so we decided to give it a new name. Here's the key points:
- Floe is free and open-source, whereas Mirage was closed-source.
- Floe is not directly tied to FrozenPlain's commercial products. It's a separate project.
- Floe is backwards-compatible with Mirage libraries and presets.
- Mirage is already the name of a hardware sampler from the 80s - we wanted to avoid confusion.

