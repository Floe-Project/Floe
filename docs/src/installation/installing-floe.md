<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Installing Floe

There's two ways to install Floe: using the installer, or manually. 

Either way, Floe is backwards-compatible. This means that you can replace an old version of Floe with a new version and everything will work.

After installing Floe, you might want to [install sample libraries](installing-libraries-and-presets.md). 

## Installer (recommended)

<!--- markdown table with --->
| Operating System | Download Link |
| --- | --- |
| Windows | [Download Windows Installer]() |

Download, unzip, and run the install file. The installer will guide you through the installation process, including choosing the plugin formats you want to install. 

Once the installation is complete you might need to restart your DAW in order for it to find the Floe plugins.

## Manual Installation

Normally you'll want to use the installer, but there could be some cases where you'd prefer to install Floe manually. To allow for this, we provide a zip file that contains Floe's plugin files.

##### Windows:
- CLAP: Move `Floe.clap` into `C:\Program Files\Common Files\CLAP`
- VST3: Move `Floe.vst3` into `C:\Program Files\Common Files\VST3`

##### macOS:
- CLAP: Move `Floe.clap` into `/Library/Audio/Plug-Ins/CLAP`
- VST3: Move `Floe.vst3` into `/Library/Audio/Plug-Ins/VST3`
- AU: Move `Floe.component` into `/Library/Audio/Plug-Ins/Components`


