<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Uninstalling

Floe doesn't yet have an uninstaller program. But you can manually uninstall it by deleting Floe's files.

### Delete libraries and presets
1. Open Floe
1. Click on the cog icon <i class="fa fa-cog"></i> at the top.
1. Open the 'Folders' tab.
1. For each of the paths click the 'open folder' icon: <i class="fa fa-external-link-square"></i>. This will open the folder in your file manager.
1. Delete all files in the folder.

Repeat this for each folder in Floe preferences panel.

### Delete preferences file and plugins
The preferences file is normally tiny: around 1 Kb in size. Plugins files are normally around 15 MB in size.

##### Windows
- Preferences: `C:\Users\Public\Floe\Preferences`
- CLAP: `C:\Program Files\Common Files\CLAP\Floe.clap`
- VST3: `C:\Program Files\Common Files\VST3\Floe.vst3`

##### macOS
- Preferences: `/Users/Shared/Floe/Preferences`
- CLAP: `/Library/Audio/Plug-Ins/CLAP/Floe.clap`
- VST3: `/Library/Audio/Plug-Ins/VST3/Floe.vst3`
- AU: `/Library/Audio/Plug-Ins/Components/Floe.component`

### Mirage locations (legacy)
If you used to have [Mirage](../about/mirage.md) installed then some additional files may be present.

##### Windows
- Preferences: `C:/Users/<your-name>/AppData/Local/FrozenPlain/Mirage`
- Preferences: `C:/ProgramData/Mirage/Settings`

##### macOS
- Preferences: `/Users/your-name/Music/Audio Music Apps/Plug-In Settings/FrozenPlain`
- Preferences: `/Library/Application Support/FrozenPlain/Mirage/Settings`
