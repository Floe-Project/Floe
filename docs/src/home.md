<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

<div class="warning">
IMPORTANT: Floe is heavily under development. This manual is a work-in-progress.

Some things may be incorrect, missing or broken. We are bit-by-bit filling in the gaps ahead of version 1.0.0.

</div>

---

<p align="center">
    <picture>
        <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/Floe-Project/Floe-Logos/HEAD/horizontal_transparent.svg">
        <source media="(prefers-color-scheme: light)" srcset="https://raw.githubusercontent.com/Floe-Project/Floe-Logos/HEAD/horizontal_transparent_dark.svg">
        <img alt="Floe" src="https://raw.githubusercontent.com/Floe-Project/Floe-Logos/HEAD/horizontal_background.svg" width="350" height="auto" style="max-width: 100%;">
    </picture>
</p>

---

<h1 style="text-align:left; font-size: 40px">A sample library player, without the fuss.</h1>
<p style="text-align:left; font-size: 20px; margin-bottom: 40px;">Floe is a free, open-source CLAP/VST3/AU plugin with powerful controls for expressive playback and sound-shaping of sample libraries in the <a href="https://floe.audio/sample-library-format.html">Floe format</a>.</p>

![Floe's GUI](https://frozenplain.com/wp-content/uploads/2019/09/wraith-2.jpg)

## Overview

Floe is a free, open-source sample library engine.

It loads sample libraries in the [Floe format](./sample-library-format.md): supporting both realistic virtual instruments and sample-based synthesis. Sample libraries are installed separately from Floe.

Floe is a CLAP, VST3 or AU plugin on <i class="fa fa-windows"></i> Windows and <i class="fa fa-apple"></i> macOS.

Developed by [FrozenPlain](https://frozenplain.com). Floe's ancestor, [Mirage](./mirage.md), has been used by thousands of musicians and producers in 13 products.

> ### Floe's mission
> 1. Make sample libraries more expressive, playable and effective in music production.
> 1. Lengthen the lifespan of sample libraries by providing a open-source platform for them with no lock-in.
> 1. Prioritise helping users make meaningful music; not technical hassle, commercial pressure or unnecessary complexity.

### Key features

#### Simple and effective 
- Three-layer architecture for blending timbres into new sounds
- Sound-shaping parameters: envelopes, filters, LFOs and an effects rack
- Supports realistic multi-sampled instruments: velocity layers, round-robin, crossfade layers **[coming soon]**
- Supports sample-based synthesis: using samples like 'oscillators' with sound-looping features
- Powerful tag-based browsers for finding new sounds and presets **[coming soon]**
- Make samples come alive with MIDI MPE support **[coming soon]**
- Combine sounds across all sample libraries
- CPU-efficient

#### Focused on helping you make music - no lock-in
- 100% free: no lock-in, no sign-up, no nagging, no features behind a paywall
- 'Organic' software: made by a passionate developer who cares about making great software and is always open to feedback
- [Open-source](https://github.com/Floe-Project/Floe), [free software](https://fsfe.org/freesoftware/freesoftware.en.html). Floe is always yours: the GPL license guarantees anyone can use, modify and share the code
- Use samples from Floe's libraries in other software: they're open FLAC/WAV files
- Thoughtfully designed; thoroughly documented

#### Sample library development
- Create your own sample libraries using Lua scripts
- Great developer-experience for creating sample libraries; just write Lua code and Floe instantly applies the changes
- No recreating GUIs: Floe provides a powerful GUI for your sample library (GUI customisation will be added in the future)

#### Continuously improving
- Regular updates 
- Always backwards-compatible


## About this website
This website contains everything you need to know about Floe. It's presented in a book-like format, with chapters in the sidebar. 

Latest Floe version: {{#include ../../version.txt}}

If you're viewing this online, you might find the search feature useful: open it by clicking the magnifying glass icon at the top of the page. Additionally, there is a printer icon in the top right for either printing this book, or saving it to a PDF.

