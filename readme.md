<!--
SPDX-FileCopyrightText: 2018-2024 Sam Windell
SPDX-License-Identifier: CC0-1.0
-->

![CI](https://github.com/Floe-Project/Floe/actions/workflows/ci.yml/badge.svg)
[![codecov](https://codecov.io/github/Floe-Project/Floe/graph/badge.svg?token=7HEJ7SF75K)](https://codecov.io/github/Floe-Project/Floe)
[![CodeFactor](https://www.codefactor.io/repository/github/floe-project/floe/badge/main)](https://www.codefactor.io/repository/github/floe-project/floe/overview/main)

<p align="center">
  <a href="https://floe.audio">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/Floe-Project/Floe-Logos/HEAD/horizontal_transparent.svg">
      <source media="(prefers-color-scheme: light)" srcset="https://raw.githubusercontent.com/Floe-Project/Floe-Logos/HEAD/horizontal_transparent_dark.svg">
      <img alt="Tailwind CSS" src="https://raw.githubusercontent.com/Floe-Project/Floe-Logos/HEAD/horizontal_background.svg" style="width: 350px; height: auto; max-width: 100%;">
    </picture>
  </a>
</p>


# [Floe](https://floe.audio) Source Code
___IMPORTANT: Large-scale changes are underway, many things are missing and broken for now.___

### Sample library engine
Floe is a CLAP, VST3 or AU plugin for Windows and macOS. It loads sample libraries in the Floe format. Visit [floe.audio](https://floe.audio) for more information about the project.

## Development Roadmap
Floe will be updated and deployed regularly with stable, backwards-compatible releases. Currently we are working towards the first stable version, 1.0. We use GitHub issues to track the [milestones](https://github.com/Floe-Project/Floe/milestones?direction=asc&sort=title&state=open) towards that.

The work so far from Mirage (the old project) to Floe v1.0 has been related to building infrastructure for long-term, sustainable development. New features and improvements will be coming once we have a strong foundation.

Additionally, once we reach v1.0, we'd like to expand the number of sample libraries available and update Mirage libraries to Floe's new sample library format.

## Status towards v1.0
1. 🟦 Completed for the current requirements
2. 🟩 Works well-enough but doesn't handle everything
3. 🟨 Mostly works but some aspects are missing or broken
4. 🟥 Fundamentally broken or not-yet-implemented

| Module                      | Description                                          | Status |
| --------------------------- | ---------------------------------------------------- | ------ |
| Sample library server       | Async loading & scanning of libraries                | 🟦     |
| Directory watcher           | Cross-platform API for watching for file changes     | 🟦     |
| CI & CD                     | Continuous integration and deployment                | 🟦     |
| State serialisation         | Saving/loading plugin state to DAW or preset         | 🟦     |
| Lua sample library format   | Sample library Lua API                               | 🟩     |
| MDATA sample library format | Legacy binary sample library format                  | 🟩     |
| Settings file               | Saving/loading settings from file                    | 🟩     |
| GUI                         | Graphical user interface                             | 🟩     |
| Audio/GUI communication     | Communication between audio and GUI threads          | 🟩     |
| Audio parameters            | System for configuring/using audio plugin parameters | 🟩     |
| Audio processing pipeline   | Sound shaping, MIDI, modulation, effects, etc.       | 🟨     |
| CLAP format                 | CLAP                                                 | 🟨     |
| VST3 format                 | VST3                                                 | 🟥     |
| AUv2 format                 | Audio Unit (v2)                                      | 🟥     |
| Presets server              | Async loading & scanning of presets                  | 🟥     |
| User manual                 | Comprehensive documentation                          | 🟥     |


## License
This project is licensed under GPL version 3 or later. See the LICENCES folder for the full licence text. We follow the [REUSE](https://reuse.software/) recommendations for this repository.

## Building
Building is done on a Linux or macOS system. Cross compilation is supported. Therefore from Linux/macOS you can build for Windows and other platforms. 

However, Linux is only provided for development purposes at the moment, and you can't cross compile to Linux from a non-Linux system.

- Install Nix and enable Flakes
- Run `nix develop` in the root of the project to enter a shell with all dependencies
- Run `just build native`. Alternative options instead of `native` are: `linux`, `windows`, `mac_arm`, `mac_x86`, `mac_ub` (universal binary)

## Development philosophy
- No frameworks, just a handful of libraries for specific tasks.
- In general, we prefer a more C-style of C++: structs and functions, memory arenas instead of new/delete/smart-pointers.
- Compile times are pretty fast and we do our best to keep them that way.
- We don't use the C++ standard library, and only sparingly use the C standard library.
- We only support one compiler: Clang (via Zig), and so we wholeheartedly use Clang-specific features and features from C++20/23 and above.
- Good tooling is a priority. The build system generates a compile_commands.json, so clangd, clang-tidy, cppcheck, etc. are all supported.
- We pair `Nix` with `just` for accessing great tools, being able to deeply integrate the project and be consistent across machines (local/CI/Linux/macOS).
- We try to focus on making the code a joy to work on: programming can be fun.
- Incrementally improve the codebase, it's still a work-in-progress.

