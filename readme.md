<!--
SPDX-FileCopyrightText: 2018-2024 Sam Windell
SPDX-License-Identifier: CC0-1.0
-->

<a href="https://floe.audio">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/Floe-Project/Floe-Logos/HEAD/horizontal_transparent.svg">
    <source media="(prefers-color-scheme: light)" srcset="https://raw.githubusercontent.com/Floe-Project/Floe-Logos/HEAD/horizontal_transparent_dark.svg">
    <img alt="Floe" src="https://raw.githubusercontent.com/Floe-Project/Floe-Logos/HEAD/horizontal_background.svg" width="250" height="auto" style="max-width: 100%;">
  </picture>
</a>

---

![CI](https://github.com/Floe-Project/Floe/actions/workflows/ci.yml/badge.svg)
[![codecov](https://codecov.io/github/Floe-Project/Floe/graph/badge.svg?token=7HEJ7SF75K)](https://codecov.io/github/Floe-Project/Floe)
[![CodeFactor](https://www.codefactor.io/repository/github/floe-project/floe/badge/main)](https://www.codefactor.io/repository/github/floe-project/floe/overview/main)

___IMPORTANT: Large-scale changes are underway, many things are missing and broken for now.___

### Sample library player
Floe is a CLAP, VST3 or AU plugin for Windows and macOS. It loads and plays sample libraries in the Floe format. Visit [floe.audio](https://floe.audio) for more information about the project. 

Join Floe's [Zulip group chat/forum](https://floe.zulipchat.com) for discussions or questions about Floe.

## Development Roadmap
Below is the current plan. Subject to change. We sometimes use GitHub issues to track [milestones](https://github.com/Floe-Project/Floe/milestones?direction=asc&sort=title&state=open) towards future releases. 

### Step 1: beta release in October/November 2024
We will release a beta version of Floe. It will be fully functional, but might have some bugs. The goal of the beta is to fix bugs before the full release.

### Step 2: release V1 before 2025
#### Complete, but not completed
We aim to provide a stable, complete version of Floe. It will be full, usable, professional software, but we are not stopping there. There are still things we want to add and improve.

On the surface, it might not look very different from Mirage (Floe's predecessor). However, there are many significant improvements which are the result of over a year of development. The focus has been on these things:
- Make the codebase amenable to years of future development: reduce technical debt, improve maintainability, automate testing and deployment.
- Improve the ability to develop new sample libraries.
- Make installing the plugin and sample libraries much less hassle, and fix issues relating to folders and files.

New features since Mirage:
- Ability for anyone to develop sample libraries
- New sample library format (Lua-based) including instant-reloading of changes
- New plugin formats (CLAP, VST3)
- New build system
- CI/CD pipeline
- New comprehensive documentation
- New more robust way to save settings
- New more expandable and faster state serialisation (in DAW and preset files)
- New settings GUI
- New more robust way to install libraries and presets (Floe packages)
- Hugely more maintainable, refactored codebase

### Step 3: expand the sample library ecosystem
Floe is not much use without sample libraries. Next step will be to expand the number of sample libraries available so that people can start properly using it. FrozenPlain will be updating their website and converting their Mirage libraries to Floe's new sample library format.

### Step 4: continued maintainability improvements
There's still some parts of Floe's codebase that are not amenable to long-term development: the GUI and the audio processing pipeline. These need to be reworked before big new features are added.

### Step 5: larger improvements and new features
The groundwork has been laid, now to enhance Floe. We will be looking at what users want as well as the following list:
- Make the GUI more consistent
- Support modern technologies: MPE, MIDI2, polyphonic modulation
- Add more features the sample-library format to allow for more complex sampling
- Add support for controlling GUI via the Lua sample libraries
- Modulation system
- Macro knobs
- Add algorithmic oscillators for layering with sample-based synthesis

## License
This project is licensed under GPL version 3 or later. See the LICENCES folder for the full licence text. We follow the [REUSE](https://reuse.software/) recommendations for this repository.

## Building
Building is done on a Linux or macOS system. Cross compilation is supported. Therefore from Linux/macOS you can build for Windows and other platforms. 

However, Linux is only provided for development purposes at the moment, and you can't cross compile to Linux from a non-Linux system.

- Install Nix and enable Flakes
- Run `nix develop` in the root of the project to enter a shell with all dependencies
- Run `just build native`. Alternative options instead of `native` are: `linux`, `windows`, `mac_arm`, `mac_x86`, `mac_ub` (universal binary)
- Binaries are created in the zig-out directory

## About the code
Floe is a [CLAP](https://github.com/free-audio/clap) plugin written in C++. The [clap-wrapper project](https://github.com/free-audio/clap-wrapper) is used to provide VST3 and AU versions. Eventually, we plan to use the Zig programming language.

### Project structure
#### Helpers
- `src/foundation`: basics, designed to be used by every file
- `src/os`: OS API abstractions: filesystem, threading, etc.
- `src/utils`: more specialised stuff building off of foundation + os
- `src/tests`: unit tests and framework for registering tests
- `src/common_infrastructure`: Floe-specific infrastructure

#### Plugin
- `src/plugin`: the actual Floe audio plugin
- `src/plugin/descriptors`: tables of information about parameters, effects
- `src/plugin/engine`: the structure that glues all non-GUI parts of the plugin together
- `src/plugin/gui`: provides the GUI for the engine
- `src/plugin/gui_framework`: framework for building the GUI
- `src/plugin/plugin`: plugin setup and interaction with the CLAP plugin API
- `src/plugin/presets`: preset file indexing
- `src/plugin/processing_utils`: utilities for processing audio
- `src/plugin/processor`: the audio processing pipeline
- `src/plugin/sample_lib_server`: system that loads and manages sample libraries
- `src/plugin/settings`: settings file interaction
- `src/plugin/state`: serialisation of engine state

#### Auxiliaries/tools
- `src/standalone_wrapper`: wraps the audio plugin in a 'standalone' app (dev only)
- `src/windows_installer`: installer for windows
- `src/gen_docs_tool`: cli tool to generate things for the mkdocs site
- `src/packager`: tool to package up libraries/presets for distribution

#### Other key components
- `justfile`: Utils for building, testing, installer building. These are designed to be run either locally or on CI.
- `.github/workflows`: Comprehensive CI/CD workflow including building & testing on all 3 OSs, valgrind, clang-tidy, spelling mistakes, clap-validator, VST3Validator, pluginval, clang-format, deploying our docs to floe.audio, creating installers, codesigning and notarization, creating releases.
- `flake.nix`: Nix provides the same set of development tools on any machine.
- `build.zig`: Zig build system for compiling our C++ codebase. Provides cross-compilation and fetches external library dependencies (see the `build.zig.zon` file).
- `docs`: Markdown files that are used by [mdBook](https://github.com/rust-lang/mdBook) to generate the documentation site.

### Code style [WIP]
- No frameworks, just a handful of libraries for specific tasks.
- In general, we prefer structs and functions rather than objects and methods, and memory arenas instead of new/delete/malloc/free/smart-pointers.
- Compile times are pretty fast and we do our best to keep them that way.
- We don't use the C++ standard library, and only sparingly use the C standard library.
- We only support one compiler: Clang (via Zig), and so we wholeheartedly use Clang-specific features and features from C++20/23 and above.
- Good tooling is a priority. The build system generates a compile_commands.json, so clangd, clang-tidy, cppcheck, etc. are all supported.
- We try to focus on making the code a joy to work on: programming can be fun.
- Incrementally improve the codebase, it's still a work-in-progress. Unfortunately there's still some technical debt to pay off.

## Inventory of features
1. 🟦 Completed enough for the current requirements
2. 🟩 Works well-enough but doesn't handle everything
3. 🟨 Mostly works but some aspects are missing or broken
4. 🟥 Fundamentally broken or not-yet-implemented

| Module                      | Description                                          | Status |
| --------------------------- | ---------------------------------------------------- | ------ |
| Sample library server       | Async loading & scanning of libraries                | 🟦     |
| Directory watcher           | Cross-platform API for watching for file changes     | 🟦     |
| CI & CD                     | Continuous integration and deployment                | 🟦     |
| State serialisation         | Saving/loading plugin state to DAW or preset         | 🟦     |
| Lua sample library format   | Sample library Lua API                               | 🟦     |
| MDATA sample library format | Legacy binary sample library format                  | 🟦     |
| Settings file               | Saving/loading settings from file                    | 🟦     |
| Audio parameters            | System for configuring/using audio plugin parameters | 🟦     |
| GUI                         | Graphical user interface                             | 🟩     |
| Audio/GUI communication     | Communication between audio and GUI threads          | 🟩     |
| Audio processing pipeline   | Sound shaping, MIDI, modulation, effects, etc.       | 🟨     |
| CLAP format                 | CLAP                                                 | 🟨     |
| VST3 format                 | VST3                                                 | 🟥     |
| AUv2 format                 | Audio Unit (v2)                                      | 🟥     |
| Presets server              | Async loading & scanning of presets                  | 🟥     |
| User manual                 | Comprehensive documentation                          | 🟥     |
