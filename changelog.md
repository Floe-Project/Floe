<!--
SPDX-FileCopyrightText: 2018-2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later

IMPORTANT: Our release process expects this file to contain heading that exactly match the released version numbers. 
For instance: 0.0.1. Don't change the headings.

-->

# Changelog

## 0.0.1
This is the first release of Floe. It's 'alpha quality' at the moment - there will be bugs and there are a couple of missing features. This release is designed mostly to test our release process.

This release only contains the CLAP plugin. The VST3 and AU plugins will be released soon.

- [Renamed from Mirage to Floe](https://floe.audio/about/mirage.html).
- Use multiple different libraries in the same instance.
- CLAP version added - VST3 and AU coming soon, VST2 support dropped.
- New installer: offline, no account/download-tickets needed. Libraries are installed separately.
- Ability for anyone to develop sample libraries using new Lua-based sample library format. The new format features a new system to tag instruments and libraries. Changes to the format are instantly applied to Floe.
- New [comprehensive documentation](https://floe.audio).
- Floe settings are saved in a more robust way avoiding issues with permissions/missing files.
- Improved default locations for saving libraries and presets to avoid permissions issues.
- New settings GUI.
- Floe now can have multiple library and preset folders. It will scan all of them for libraries and presets. This is a more robust and flexible way to manage assets rather than trying to track individual files.
- Floe packages: a robust way to install libraries and presets. Floe packages are zip files in a particular layout that contain libraries and presets. In the settings of Floe, you can install these packages. It handles all the details of installing/updating libraries and presets. It checks for existing installations and updates them if needed. It even checks if the files of any existing installation have been modified and will ask if you want to replace them. Everything just works as you'd expect.
- New format for saving presets and DAW state - smaller and faster than before and allows for more expandability.

Technical changes:
- Huge refactor of the codebase to be more maintainable and expandable. There's still more to be done yet though.
- New build system using Zig; cross-compilation, dependency management, etc.
- Comprehensive CI/CD pipeline for testing and creating release builds.
- New 'sample library server', our system for providing libraries and audio files to Floe in a fast, async way.

## Mirage
Prior to 0.0.1, Floe was known as Mirage. The last Mirage version was 2.0.5.
