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

Repeat this for each folder in Floe settings panel.

### Delete settings and plugins
Settings are tiny files that store your preferences. Plugins files are normally ~15 MB in size.

#### Windows
- Settings: `C:\Users\Public\Floe\Settings`
- CLAP: `C:\Program Files\Common Files\CLAP\Floe.clap`
- VST3: `C:\Program Files\Common Files\VST3\Floe.vst3`

#### macOS
- Settings: `/Users/Shared/Floe/Settings`
- CLAP: `/Library/Audio/Plug-Ins/CLAP/Floe.clap`
- VST3: `/Library/Audio/Plug-Ins/VST3/Floe.vst3`
- AU: `/Library/Audio/Plug-Ins/Components/Floe.component`

### Mirage locations (legacy)
If you used to have [Mirage](../about/mirage.md) installed then some additional files may be present.

#### Windows
- Settings: `C:/Users/<your-name>/AppData/Local/FrozenPlain/Mirage`
- Settings: `C:/ProgramData/Mirage/Settings`

#### macOS
- Settings: `/Users/your-name/Music/Audio Music Apps/Plug-In Settings/FrozenPlain`
- Settings: `/Library/Application Support/FrozenPlain/Mirage/Settings`
