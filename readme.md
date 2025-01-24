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

___IMPORTANT: Large-scale changes are underway, many things are missing and broken for now.___

### Sample library player
Floe is a CLAP, VST3 or AU plugin for Windows and macOS. It loads and plays sample libraries in the Floe format. Visit [floe.audio](https://floe.audio) for more information about the project. 

## Roadmap
See our [roadmap](https://floe.audio/about/roadmap) section our our website. We also sometimes use GitHub issues to track [milestones](https://github.com/Floe-Project/Floe/milestones?direction=asc&sort=title&state=open) towards future releases. 

## License
This project is licensed under GPL version 3 or later. See the LICENCES folder for the full licence text. We follow the [REUSE](https://reuse.software/) recommendations for this repository.

## Building
Building is done on a Linux or macOS system. Cross-compilation is supported. Therefore from Linux/macOS you can build for Windows and other platforms. 

Linux is only used for development purposes at the moment, and you can't cross-compile to Linux from a non-Linux system.

- Install Nix and enable Flakes
- Run `nix develop` in the root of the project to enter a shell with all dependencies
- Run `just build native`. Alternative options instead of `native` are: `linux`, `windows`, `mac_arm`, `mac_x86`, `mac_ub` (universal binary)
- Binaries are created in the zig-out directory

## About the code
Floe is a [CLAP](https://github.com/free-audio/clap) plugin written in C++. The [clap-wrapper project](https://github.com/free-audio/clap-wrapper) is used to provide VST3 and AU versions. Eventually, we plan to use the Zig programming language.

## Discussion
Feel free to use the discussions on GitHub for questions, feedback, and ideas. Report bugs to the Github issue tracker. Also, FrozenPlain has a Floe section on their [forum](https://forum.frozenplain.com).

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
- `src/docs_preprocessor`: tool to preprocess markdown files for the docs
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
- Try not to crash the host. When we enter a 'panic' state we make reasonable efforts to be harmless instead of crashing the host.
- Incrementally improve the codebase, it's still a work-in-progress. Unfortunately there's still some technical debt to pay off.

### CI
![CI](https://github.com/Floe-Project/Floe/actions/workflows/ci.yml/badge.svg)
[![codecov](https://codecov.io/github/Floe-Project/Floe/graph/badge.svg?token=7HEJ7SF75K)](https://codecov.io/github/Floe-Project/Floe)
[![CodeFactor](https://www.codefactor.io/repository/github/floe-project/floe/badge/main)](https://www.codefactor.io/repository/github/floe-project/floe/overview/main)
