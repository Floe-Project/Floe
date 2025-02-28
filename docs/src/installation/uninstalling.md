<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Uninstalling

## Windows

On Windows, Floe can be uninstalled using the standard method for uninstalling software, via the 'Add or Remove Programs' control panel. See the [Windows documentation here](https://support.microsoft.com/en-gb/windows/uninstall-or-remove-apps-and-programs-in-windows-4b55f974-2cc6-2d2b-d092-5905080eaf98).

This uninstalls Floe, but not libraries or presets. Follow the instructions below to delete libraries and presets.

This uninstaller also supports uninstalling Mirage, the predecessor to Floe.

## macOS

We don't have an uninstaller program for macOS. But you can manually uninstall Floe by deleting its files.


## Manual Uninstall

### Delete libraries and presets
1. Open Floe
1. Click on the cog icon <i class="fa fa-cog"></i> at the top.
1. Open the 'Folders' tab.
1. For each of the paths click the 'open folder' icon: <i class="fa fa-external-link-square"></i>. This will open the folder in your file manager.
1. Delete all files in the folder.

Repeat this for each folder in Floe preferences panel.

### Delete Floe
Floe consists of plugin files, and a preferences file. The preferences file is normally tiny: around 1 Kb in size. Plugins files are normally around 15 MB in size.

Delete the following files:

##### macOS
- Preferences (may not exist): `/Users/Shared/Floe/Preferences/floe.ini`
- CLAP: `/Library/Audio/Plug-Ins/CLAP/Floe.clap`
- VST3: `/Library/Audio/Plug-Ins/VST3/Floe.vst3`
- AU: `/Library/Audio/Plug-Ins/Components/Floe.component`

##### Windows
For Windows, use the uninstaller program described above. If you want to manually delete Floe, delete the following files:
- Preferences (may not exist): `C:\Users\Public\Floe\Preferences\floe.ini`
- CLAP: `C:\Program Files\Common Files\CLAP\Floe.clap`
- VST3: `C:\Program Files\Common Files\VST3\Floe.vst3`


### Delete Mirage
If you used to have [Mirage](../about/mirage.md) installed then some additional files may be present. To uninstall Mirage, delete the following files. Some of these files may not exist.

##### macOS
- Preferences: `/Library/Application Support/FrozenPlain/Mirage/Settings/mirage.json`
- Preferences (alternate): `/Users/<your-name>/Music/Audio Music Apps/Plug-In Settings/FrozenPlain/mirage.json`
- Preferences (alternate): `/Users/<your-name>/Library/Application Support/FrozenPlain/Mirage/Settings/mirage.json`
- VST2: `/Library/Audio/Plug-Ins/VST/Mirage.vst`
- AU: `/Library/Audio/Plug-Ins/Components/FrozenPlain Mirage.component`

##### Windows
- Preferences: `C:\ProgramData\Mirage\Settings\mirage.json`
- Preferences (alternate): `C:\Users\<your-name>\AppData\Local\FrozenPlain\Mirage\mirage.json`
- VST2: `C:\Program Files\VSTPlugins\mirage64.dll`
- VST2 (alternate): `C:\Program Files\Steinberg\VSTPlugins\mirage64.dll`
- VST2 (alternate): `C:\Program Files\Common Files\VST2\mirage64.dll`
- VST2 (alternate): `C:\Program Files\Common Files\Steinberg\VST2\mirage64.dll`
