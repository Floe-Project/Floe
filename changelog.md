<!--
SPDX-FileCopyrightText: 2018-2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later

IMPORTANT: Our release process expects this file to contain heading that exactly match the released version numbers. 
For instance: 0.0.1. Don't change the headings.

-->

# Changelog

## 0.0.3
- Rename 'settings' to 'preferences'
- Add support for packaging and installing MDATA libraries (Mirage)
- Add a Floe uninstaller for Windows, integrated into Windows' 'Installed Apps' control panel
- Add tooltips to the preferences GUI
- Add new Info panel featuring info about installed libraries. 'About', 'Metrics' and 'Licenses' have been moved here too instead of being separate panels.
- Add new Share Feedback panel for submitting bug reports and feature requests
- Add new fields to the Lua API to support license info and attribution, such as CC BY
- Add attribution-required panel which appears when needed with generated copyable text for fulfilling attribution requirements. Synchronised between all instances of Floe. Makes complying with licenses like CC BY easy.
- Add error reporting. We are now better able to fix bugs. When an error occurs, an anonymous report is sent to us. You can disable this in the preferences. This pairs with the new Share Feedback panel - that form can also be used to report bugs.
- Add autosave feature, which efficiently saves the current state of Floe at a configurable interval. This is useful for recovering from crashes. Configurable in the preferences.
- Add ability to select multiple packages to install at once
- Preferences system is more robust and flexible. Preferences are saved in a small file. It syncs between all instances of Floe - even if the instances are in different processes. Additionally, you can edit the preferences file directly if you want to; the results will be instantly reflected in Floe.
- Renamed Appearance preferences to General since it's small and can be used for other preferences
- Make notifications dismiss themselves after a few seconds
- Fixed externally deleted or moved-to-trash libraries not being removed from Floe
- Fixed not installing to the chosen location
- Make window resizing work better: fixed aspect ratio, correct remembering of previous size, correct keyboard show/hide, resizable to any size within a reasonable range.

## 0.0.2
- Fix Windows installer crash
- Don't show a console window with the Windows installer
- Better logo for Windows installer
- Remove unnecessary 'Floe Folders' component from macOS installer

## 0.0.1
This is the first release of Floe. It's 'alpha quality' at the moment - there will be bugs and there are a couple of missing features. This release is designed mostly to test our release process.

This release only contains the CLAP plugin. The VST3 and AU plugins will be released soon.

## Mirage
Floe used to be called [Mirage](https://floe.audio/about/mirage.html). Mirage contained many of the same features seen in Floe v0.0.1. But there are large structural changes, and some new features and improvements:

- Use multiple different libraries in the same instance.
- CLAP version added - VST3 and AU coming soon, VST2 support dropped.
- New installer: offline, no account/download-tickets needed. Libraries are installed separately.
- Ability for anyone to develop sample libraries using new Lua-based sample library format. The new format features a new system to tag instruments and libraries. Hot-reload library development: changes to a library are instantly applied to Floe.
- New [comprehensive documentation](https://floe.audio).
- Floe now can have multiple library and preset folders. It will scan all of them for libraries and presets. This is a much more robust and flexible way to manage assets rather than trying to track individual files.
- New, robust infrastructure:
    - Floe settings are saved in a more robust way avoiding issues with permissions/missing files.
    - Improved default locations for saving libraries and presets to avoid permissions issues.
    - New format for saving presets and DAW state - smaller and faster than before and allows for more expandability.
- New settings GUI.
- Floe packages: a robust way to install libraries and presets. Floe packages are zip files with a particular layout that contain libraries and presets. In the settings of Floe, you can install these packages. It handles all the details of installing/updating libraries and presets. It checks for existing installations and updates them if needed. It even checks if the files of any existing installation have been modified and will ask if you want to replace them. Everything just works as you'd expect.

Technical changes:
- Huge refactor of the codebase to be more maintainable and expandable. There's still more to be done yet though.
- New build system using Zig; cross-compilation, dependency management, etc.
- Comprehensive CI/CD pipeline for testing and creating release builds.
- New 'sample library server', our system for providing libraries and audio files to Floe in a fast, async way.
