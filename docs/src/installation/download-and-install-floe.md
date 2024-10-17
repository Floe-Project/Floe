<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Download & Install Floe

There's two ways to install Floe: use the installer, or manually move files. 

Either way, Floe is backwards-compatible. This means that you can replace an old version of Floe with a new version and everything will work.

After installing Floe, you might want to [install sample libraries](install-libraries-and-presets.md) or [develop your own](../develop/develop-libraries.md) (programming required). 

## Installer

**<i class="fa fa-windows"></i> Floe Windows Installer**: [Download Floe-Installer-v{{#include ../../mdbook_config.txt:latest-release-version}}-Windows.zip](https://github.com/Floe-Project/Floe/releases/download/v{{#include ../../mdbook_config.txt:latest-release-version}}/Floe-Installer-v{{#include ../../mdbook_config.txt:latest-release-version}}-Windows.zip)

**<i class="fa fa-apple"></i> Floe macOS Installer**: [Download Floe-Installer-v{{#include ../../mdbook_config.txt:latest-release-version}}-macOS.zip](https://github.com/Floe-Project/Floe/releases/download/v{{#include ../../mdbook_config.txt:latest-release-version}}/Floe-Installer-v{{#include ../../mdbook_config.txt:latest-release-version}}-macOS.zip)

Download, unzip, and run the installer program. The installer will guide you through the installation process, including choosing the plugin formats you want to install. 

Once the installation is complete you might need to restart your DAW in order for it to find the Floe plugins.

## Manual Installation

**<i class="fa fa-windows"></i> Floe Windows Manual Install**: [Download Floe-Manual-Install-v{{#include ../../mdbook_config.txt:latest-release-version}}-Windows.zip](https://github.com/Floe-Project/Floe/releases/download/v{{#include ../../mdbook_config.txt:latest-release-version}}/Floe-Manual-Install-v{{#include ../../mdbook_config.txt:latest-release-version}}-Windows.zip)

**<i class="fa fa-apple"></i> Floe macOS Manual Install**: [Download Floe-Manual-Install-v{{#include ../../mdbook_config.txt:latest-release-version}}-macOS.zip](https://github.com/Floe-Project/Floe/releases/download/v{{#include ../../mdbook_config.txt:latest-release-version}}/Floe-Manual-Install-v{{#include ../../mdbook_config.txt:latest-release-version}}-macOS.zip)



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
