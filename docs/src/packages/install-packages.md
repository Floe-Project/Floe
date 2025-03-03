<!--
SPDX-FileCopyrightText: 2025 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Installing Packages

Floe can install sample libraries and presets from ZIP files called _Floe packages_.

## Install using Floe

![Floe's GUI](../images/install-package-gui.png)

__Don't extract the ZIP file__. Instead, open Floe and use the 'Install package' button in the preferences panel and direct it to the ZIP file. It will extract the package into the correct folders. The library/presets will be available immediately, no need to restart Floe.

### Here's the full details

1. Open Floe.
1. Open the Preferences panel using the <i class="fa fa-cog"></i> cog icon at the top.
1. Open the Packages tab.
1. Click the 'Install package' button and direct it to the package ZIP file.
1. Done. After installation, you may delete the ZIP file. No need to restart Floe.

### Smart installation

Floe handles the installation process intelligently. It will check existing installations across all known folders, check for conflicts and handle upgrades. It will even detect if you've modified a library and give you the option to keep your modified version. It will ask you about skipping or overwriting if it needs too. It will never duplicate libraries unnecessarily.

## Manually installing

Alternatively, you can manually install libraries and presets by extracting the ZIP file into the correct folders.

1. Extract the package ZIP file (floe.zip).
1. Open Floe.
1. Open the Preferences panel using the gear icon at the top.
1. Open the Folders tab.
1. Here you can control which folders Floe looks for libraries and presets in. Copy/move the contents of this package's Libraries into one of Floe's library folders, and the same for Presets into one Floe's presets folder.
1. Done. No need to restart Floe.
