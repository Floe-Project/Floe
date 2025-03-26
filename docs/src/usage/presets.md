<!--
SPDX-FileCopyrightText: 2025 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Presets

Floe can save its current state as a preset file, which can be loaded later. It's the same file type regardless of what sample libraries you're using; a `.floe-preset` file. When you load a preset, Floe will check that you have the required libraries installed, and if not, it will show an error.

Presets are portable - you can copy them to other computers or operating systems. You can rearrange them into folders and rename them as you like.

## Preset browser

![Preset Browser GUI](../images/preset-browser.png)

Floe features a browser for conveniently navigating and loading presets from your preset folders. This browser has two panels. The panel on the left is used to select the folder to browse. The panel on the right is used to load presets from within the selected folder and its subfolders. You can use the arrow keys on your keyboard to move to different presets on the right panel.

You can also search for folders or files by typing into the search bar on this panel. Your search term is compared against each filepath of every preset in the currently shown folder.


## Preset folders

![Folder Preferences GUI](../images/folder-preferences.png)

Floe automatically scans for presets in a set of folders - including subfolders. This works in the exact same way as your [library folders](./sample-libraries.md).

Presets are tiny files and so there's typically no need to move then to an external hard-drive.

