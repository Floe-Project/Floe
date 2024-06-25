<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Manual Installation
Normally you'll want to use the installer, but there could be some cases where you'd prefer to install Floe manually. To allow for this, we provide a zip file that contains Floe's plugin files. 

Be careful with manual installation: things can get complicated if you have audio plugins installed to multiple places on your computer. If you're not sure, use the installer instead.

You can copy these files to wherever your DAW looks for plugins. You can overwrite any exsiting Floe installation because Floe is backwards-compatibile. Here are the typical installation locations:

### Windows:
- CLAP (_Floe.clap_): `C:\Program Files\Common Files\CLAP`
- VST3 (_Floe.vst3_): `C:\Program Files\Common Files\VST3`

### macOS:
- CLAP (_Floe.clap_): `/Library/Audio/Plug-Ins/CLAP`
- VST3 (_Floe.vst3_): `/Library/Audio/Plug-Ins/VST3`
- AU (_Floe.component_): `/Library/Audio/Plug-Ins/Components`

Next, you might need to [install sample libraries](installing-libraries.md). 


