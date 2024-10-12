<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Developing Libraries for Floe

This document explains the details of Floe's sample library format and how to develop libraries for it. Developing libraries for Floe does require some basic programming knowledge.

Be sure to read the [About Sample Libraries](../about/sample-libraries.md) document first.

## Why a new format?
No existing format met our required criteria for Floe:
- Libraries should be plain folders of audio files.
- Libraries should be portable across filesystems & operating systems.
- Libraries should be configured using a proper programming language to enable creating complex libraries in a maintainable way.
- The format should be extensible - allowing us to innovate in the field of sampling.
- The solution should easily integrate into Floe's codebase.

## The structure

Let's look at the structure of a Floe sample library:

```
📂FrozenPlain - Slow/
├── 📄slow.floe.lua
├── 📄Licence.html
├── 📄About Slow.html
├── 📁Samples/
│   ├── 📄synth_sustain_c4.flac
│   └── 📄synth_sustain_d4.flac
└── 📁Images/
    ├── 📄background.png
    └── 📄icon.png
```

There's only one essential part of a Floe sample library: the `floe.lua` file. This file can also end with `.floe.lua` - for example, `woodwind-textures.floe.lua`.

The rest of the structure are conventions that are recommended but not required:
- **Licence**: Sample libraries are recommended to have a file called Licence that describes the terms of use of the library. It can be any format.
- **About**: A file that describes the library. Any file format. Information about a library is useful for when someone might not have Floe's GUI available.
- **Library folder name**: The folder containing the `floe.lua` file should be named: "Developer - Library Name".
- **Subfolders**: Subfolders are up to the developer. We recommend 'Samples' for audio files and 'Images' for images. These can have any number of subfolders. Your `floe.lua` file can reference any file in the library folder.

## The `floe.lua` file

The `floe.lua` file is the most important part of a library. It's a script that maps and configures the audio files into playable instruments, written in the Lua {{#include ../../mdbook_config.txt:lua-version}} programming language.

Here's an example of a `floe.lua` file:
```lua
{{#include ../../sample-library-example-no-comments.lua}}
```

This file uses [Floe's Lua API](library-lua-api.md) to create the library, create instruments, and add regions and impulse responses. It references the audio files in the library using relative paths.

`floe.new_library()` must be called and returned it at the end of the script. All other features are optional. When Floe runs your Lua file, it will show you any errors that occur along with a line number and a description.



## How-to get started

1. Create a new folder in one of Floe's [sample library folders](../about/sample-libraries.md#your-library-folders) called 'My name - My library'.
1. Create a file in that folder called `my-library.floe.lua`.
1. Open the Lua file in your text editor.
1. Use the `floe.new_library()` function to create your library, filling in all the fields marked `[required]` in the [API reference](library-lua-api.md).
1. At the end of the file, return the library object you just created.
1. Open Floe.

