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

If you're viewing this online, you might find the search feature useful: open it by clicking the magnifying glass icon at the top of the page. Additionally, there is a printer icon in the top right for either printing this book, or saving it to a PDF.

## What is Floe?
Floe is a sample-based synthesis plugin developed by [FrozenPlain](https://frozenplain.com). It's free and open-source and developed separately from FrozenPlain's commercial products. The project is hosted on [GitHub](https://github.com/Floe-Project/Floe).

Floe requires sample libraries. These are not included with the plugin. At the moment only FrozenPlain offer paid and free sample libraries, but we are looking to rapidly expand the number of libraries available, both free and paid. You can also make your own sample libraries using our Lua sample-library format.

Key features:
- CLAP, VST3 and AU plugin formats on Windows and macOS (Linux support is planned)
- Three-layer architecture for blending timbres into new sounds
- Designed to easily create playable and expressive instruments
- Sound shaping tools: filters, envelopes, LFOs and an effects rack
- Powerful browsers for samples and presets
- Multisampled instruments: velocity layers, round-robin, crossfade layers
- Create your own sample libraries using Lua scripts
- Built to last: [free software](https://fsfe.org/freesoftware/freesoftware.en.html), GPLv3 licensed and new infrastructure for long-term development

Floe was [previously known as Mirage](./mirage.md).

