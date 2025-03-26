<!--
SPDX-FileCopyrightText: 2018-2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later

IMPORTANT: Our release process expects this file to contain heading that exactly match the released version numbers. 
For instance: 0.0.1. Don't change the headings.

-->

# Changelog

## 0.0.8-alpha
- Brand new instrument browser supporting tags, search, and filtering by library
- Brand new impulse response browser featuring all the options of the instrument browser
- Brand new preset browser with tags, search, and filtering by library. Preset metadata is tracked, library information is tracked, file changes are detected, duplicate presets are hidden.
- New save preset dialog with author, description, and tags
- Fix instrument left/right and randomise buttons

## 0.0.7-alpha
The focus of this version has been bug fixes; in particular around loading Mirage libraries and presets.
- Max voice is increased from 32 to 256 allowing for more complex instruments
- Show the instrument type on the GUI: single sample, multisample or oscillator waveform
- Rename 'Dynamics' knob to 'Timbre' and fix its behaviour - for instruments such as Arctic Strings, it can be used to crossfade between different sets of samples.
- Fix missing code signing on Windows installer resulting in 'Unknown Publisher' warning
- Fix layer filter type menu being the incorrect width
- Fix crash when loop points were very close together
- Improve loop modes on GUI: it's obvious when a loop is built-in or custom, what modes are available for a given instrument, why loop modes are invalid.
- Add docs about looping
- Fix sustain pedal incorrectly ending notes

Mirage loading:
- Fix incorrect loading of Mirage on/off switches - resulting in parameters being on when they should be off
- Fix incorrect handling of Mirage's 'always loop' instruments
- Fix incorrect conversion from Mirage's effects to Floe's effects
- Improve sound matching when loading Mirage presets
- Fix failure loading some FrozenPlain Squeaky Gate instruments

Library creation:
- Re-organise the fields for add_region - grouping better into correct sections and allowing for easier expansion in the future.
- Sample library region loops now are custom tables with `start_frame`, `end_frame`, `crossfade` and `mode` fields instead of an array.
- Add `always_loop` and `never_loop` fields to sample library regions allowing for more control over custom loop usage on Floe's GUI.
- Show an error if there's more than 2 velocity layers that are using 'feathering' mode. We don't support this yet. Same for timbre layers.

## 0.0.6-alpha
- Add VST3 support
- Standardise how tags and folders will be used in instruments, presets and impulse responses: https://floe.audio/develop/tags-and-folders.html
- Fix sustain pedal
- Remove 'retrig CC 64' parameter from layer MIDI. This was mostly a legacy workaround from Mirage. Instead, we just use the typical behaviour that when you play the same note multiple times while holding the sustain pedal, the note plays again - stacking up.
- Fix package installation crash after removing folders
- Fix markers staying on ADSR envelope even when sound is silent
- Fix MIDI transpose causing notes to never stop
- Fix peak meters dropping to zero unexpectedly
- Fix not finding Mirage libraries/presets folders

## 0.0.5-alpha
- Fix text being pasted into text field when just pressing 'V' rather than 'Ctrl+V' 
- Windows: fix unable to use spacebar in text fields due to the host stealing the keypress
- Fix crash when trying to load or save a preset from file
- Improve the default background image

## 0.0.4-alpha
Fix crash when opening the preset browser.

## 0.0.3-alpha
Version 0.0.3-alpha is a big step towards a stable release. There's been 250 changed files with 17,898 code additions and 7,634 deletions since the last release. 

It's still alpha quality, and CLAP only. But if you're feeling adventurous, we'd love for you to try it out and give us feedback:
- [Download Floe](https://floe.audio/installation/download-and-install-floe.html)
- [Download some libraries](https://floe.audio/packages/available-packages.html)
- [Install libraries](https://floe.audio/packages/install-packages.html)

Error reporting has been a significant focus of this release. We want to be able to fix bugs quickly and make Floe as stable as possible. A part of this is a new a Share Feedback panel on the GUI - please use this!

Floe's website has been filled out a lot too.

New/edited documentation pages:
- [Floe support for using CC BY libraries](https://floe.audio/usage/attribution.html)
- [Autosave feature added](https://floe.audio/usage/autosave.html)
- [New error reports, crash protection, feedback form](https://floe.audio/usage/error-reporting.html)
- [New uninstaller for Windows](https://floe.audio/installation/uninstalling.html)

### Highlights
- Add new Info panel featuring info about installed libraries. 'About', 'Metrics' and 'Licenses' have been moved here too instead of being separate panels.
- Add new Share Feedback panel for submitting bug reports and feature requests
- Add attribution-required panel which appears when needed with generated copyable text for fulfilling attribution requirements. Synchronised between all instances of Floe. Makes complying with licenses like CC BY easy.
- Add new fields to the Lua API to support license info and attribution, such as CC BY
- Add lots of new content to floe.audio
- Add error reporting. We are now better able to fix bugs. When an error occurs, an anonymous report is sent to us. You can disable this in the preferences. This pairs with the new Share Feedback panel - that form can also be used to report bugs.
- Add autosave feature, which efficiently saves the current state of Floe at a configurable interval. This is useful for recovering from crashes. Configurable in the preferences.
- Add a Floe uninstaller for Windows, integrated into Windows' 'Add or Remove Programs' control panel
- Preferences system is more robust and flexible. Preferences are saved in a small file. It syncs between all instances of Floe - even if the instances are in different processes. Additionally, you can edit the preferences file directly if you want to; the results will be instantly reflected in Floe.
- Improve window resizing: fixed aspect ratio, correct remembering of previous size, correct keyboard show/hide, resizable to any size within a reasonable range.

### Other changes
- Add support for packaging and installing MDATA libraries (Mirage)
- Add tooltips to the preferences GUI
- Add ability to select multiple packages to install at once
- Rename 'settings' to 'preferences' everywhere
- Rename 'Appearance' preferences to 'General' since it's small and can be used for other preferences
- Make notifications dismiss themselves after a few seconds
- Fix externally deleted or moved-to-trash libraries not being removed from Floe
- Fix not installing to the chosen location
- Fix Windows installer creating nonsense CLAP folder
- Fix packager adding documents into the actual library rather than the package


## 0.0.2
- Fix Windows installer crash
- Don't show a console window with the Windows installer
- Better logo for Windows installer
- Remove unnecessary 'Floe Folders' component from macOS installer


## 0.0.1
This is the first release of Floe. It's 'alpha quality' at the moment - there will be bugs and there are a couple of missing features. This release is designed mostly to test our release process.

This release only contains the CLAP plugin. The VST3 and AU plugins will be released soon.


## Mirage
Floe used to be called [Mirage](https://floe.audio/about-the-project/mirage.html). Mirage contained many of the same features seen in Floe v0.0.1. But there are large structural changes, and some new features and improvements:

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

<!--
Notable changes from the last Mirage version to Floe v1.0.0:
- CLAP and VST3
- Load instruments from different libraries in same patch
- New format for creating sample libraries - open to everyone
- New settings GUI
- New installer, offline and hassle-free
- Ability to install libraries and presets (packages) with a button - handles all the details automatically
- Voice count increased from 32 to 256
- New comprehensive documentation: https://floe.audio
- Add ability to have multiple library and preset folders
- Show the type of instrument below the waveform: multisample, single sample or waveform oscillator
- 'CC64 Retrigger' parameter removed - retrigger is the default now
- Change behaviour when a volume envelope if off - it now plays the sample all the way through, without any loops, playing the same note again will stack the sound
- Show the loop mode more clearly on the GUI - it's obvious when a loop is built-in or custom, what modes are available for a given instrument, why loop modes are invalid.
- New format for saving presets and DAW state - smaller and faster than before and allows for more expandability
- Improved default locations for saving libraries and presets to avoid permissions issues
- New GUI for picking instruments, presets and convolution reverb impulse responses: filter by library, tags; search.
- Floe is open source
- Floe's codebase is vastly more maintainable and better designed - huge work was put into creating a solid foundation to build on.
- Floe has systems in place for regularly releasing updates and new features
- Fixed issue with FrozenPlain - Arctic Strings crossfade layers not working
- Fixed issue where loop crossfade wouldn't be applied - resulting in pops
- Fixed issue where Mirage would spend forever trying to open
-->
