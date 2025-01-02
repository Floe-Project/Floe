<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

<div class="warning">
IMPORTANT: Floe is heavily under development. Expect bugs, missing features and broken things.

We are bit-by-bit filling in the gaps ahead of version 1.0.0.
</div>

# Download & Install Floe

There's two ways to install Floe: use the installer, or manually move files. 

Either way, Floe is backwards-compatible. This means that you can replace an old version of Floe with a new version and everything will work.

Check the [requirements](requirements.md) before downloading. After installing Floe, you might want to [install sample libraries](install-libraries-and-presets.md) or [develop your own](../develop/develop-libraries.md) (programming required). 

> The latest released version of Floe is v==latest-release-version==.


## Installer

<img src="../images/installer-macos-gui.png" width="49%" style="display: inline;">
<img src="../images/installer-windows-gui.png" width="49%" style="display: inline;">

**<i class="fa fa-windows"></i> Floe Installer Windows**: [Download ==Floe-Installer-Windows-filename==](==Floe-Installer-Windows-url==) (==Floe-Installer-Windows-size-mb==)

**<i class="fa fa-apple"></i> Floe Installer macOS**: [Download ==Floe-Installer-macOS-filename==](==Floe-Installer-macOS-url==) (==Floe-Installer-macOS-size-mb==)

Download, unzip, and run the installer program. The installer will guide you through the installation process, including choosing the plugin formats you want to install. 

Once the installation is complete you might need to restart your DAW in order for it to find the Floe plugins.

## Manual Installation

**<i class="fa fa-windows"></i> Floe Manual Install Windows**: [Download ==Floe-Manual-Install-Windows-filename==](==Floe-Manual-Install-Windows-url==) (==Floe-Manual-Install-Windows-size-mb==)

**<i class="fa fa-apple"></i> Floe Manual Install macOS**: [Download ==Floe-Manual-Install-macOS-filename==](==Floe-Manual-Install-macOS-url==) (==Floe-Manual-Install-macOS-size-mb==)


Normally you'll want to use the installer, but there could be some cases where you'd prefer to install Floe manually. To allow for this, we provide a zip file that contains Floe's plugin files. Extract it and move the files to your plugin folders.

##### Windows:
- CLAP: Move `Floe.clap` into `C:\Program Files\Common Files\CLAP`
- VST3: Move `Floe.vst3` into `C:\Program Files\Common Files\VST3`

##### macOS:
- CLAP: Move `Floe.clap` into `/Library/Audio/Plug-Ins/CLAP`
- VST3: Move `Floe.vst3` into `/Library/Audio/Plug-Ins/VST3`
- AU: Move `Floe.component` into `/Library/Audio/Plug-Ins/Components`

## 

---

Download links can also be found on the [Github releases page](https://github.com/Floe-Project/Floe/releases/latest).
