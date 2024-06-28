<!--
SPDX-FileCopyrightText: 2018-2024 Sam Windell
SPDX-License-Identifier: CC0-1.0
-->

![CI](https://github.com/Floe-Synth/Floe/actions/workflows/ci.yml/badge.svg)
[![codecov](https://codecov.io/github/Floe-Synth/Floe/graph/badge.svg?token=7HEJ7SF75K)](https://codecov.io/github/Floe-Synth/Floe)

# Floe Source Code

___IMPORTANT: Large-scale changes are underway, many things are missing and broken for now.___

### Sample-based synthesiser plugin

This is the source code for Floe, a sample-based synthesiser plugin. It's [free software](https://fsfe.org/freesoftware/freesoftware.en.html) software developed by [FrozenPlain](https://frozenplain.com). It's available as a CLAP, VST3 or AU audio plugin for Windows and macOS. Linux is supported only for development purposes.

Floe requires sample libraries. These are created using the Lua programming language. Alternatively, FrozenPlain offer sample libraries to buy, and a few free ones.

## Documentation
We aim to offer comprehensive documentation in the form of a user manual. We use mdbook to generate this based on the markdown files in this repository.

[Online user manual](https://floe-synth.github.io/Floe/)

## Roadmap
We want Floe to be a constantly evolving and improving project with regular stable, backwards-compatible releases. Currently we are working towards the first stable version, 1.0.

- [x] Make open-source
- [x] Implement comprehensive CI/CD to aid rapid development
- [ ] Fix bugs and essential to-dos
- [ ] 0.1 alpha release?
- [ ] Implement GUI for new features (mixing instruments for different libraries)
- [ ] Overhaul audio engine: better internals for parameters, thread-safety and refactoring
- [ ] Version 1.0 release?
- [ ] Overhaul GUI internals: refactor, add proper layout system

## Previously known as Mirage
Floe is backwards-compatible with Mirage's libraries and presets. See more inforation about this in the [user manual](https://floe-synth.github.io/Floe/mirage.html).

## License
This project is licensed under GPL version 3 or later. See the LICENCES folder for the full licence text. We follow the [REUSE](https://reuse.software/) recommendations for this repository.

## Building
Building is done on a Linux or macOS system. Cross compilation is supported. Therefore from Linux/macOS you can build for Windows and other platforms. 

However, Linux is only provided for development purposes at the moment, and you can't cross compile to Linux from a non-Linux system.

- Install Nix and enable Flakes
- Run `nix develop` in the root of the project to enter a shell with all dependencies
- Run `zig build compile -Dtargets=native`. Alternative options instead of `native` are: `linux`, `windows`, `mac_arm`, `mac_x86`, `mac_ub` (universal binary)

## Interesting development things
- Compile times are pretty fast and we do our best to keep them that way.
- We don't use the C++ standard library, and only sparingly use the C standard library.
- We only support one compiler: Clang (via Zig), and so we wholeheartedly use Clang-specific features and features from C++20/23 and above.
- Good tooling is a priority. The build system generates a compile_commands.json, so clangd, clang-tidy, cppcheck, etc. are all supported.
- We pair `Nix` with `just` for acessing great tools, being able to deeply integrate the project and be consistent across machines (local/CI/Linux/macOS).
