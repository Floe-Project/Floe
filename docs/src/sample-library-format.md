<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Floe Library Format

## Overview

Floe uses a custom sample library format. It's an open format consisting of a folder of audio files plus a file called `floe.lua`. The Lua file describes how the samples are mapped and configured. Lua is a widely-used programming language that is easy to use.

> Openness is a key goal of Floe sample libraries. There's no proprietary file formats. There's just FLAC, WAV and Lua. By using widely-available file formats we ensure the longevity of libraries. Additionally, access to the audio files gives you the freedom to use them in other software.

Floe provides easy-to-use tools for installing Floe sample libraries. You don't need to worry about this format's details unless you're interested in creating your own libraries (which does require some basic programming knowledge).

## Format details

### Overall structure

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

### How Floe reads libraries

Floe automatically scans for libraries in its sample library folders - these are configurable in Floe's settings. Floe scans nested subfolders too. Floe automatically detects when files are added, removed or changed, and will immediately apply the changes, showing any errors if they occur. So all you need to do to start developing a library is to create a folder with a `floe.lua` file in it. This was a lot of work to implement but it unlocks rapid development of libraries.

### `floe.lua` file
Let's start with a simple example of a `floe.lua` file:
```lua
{{#include ../sample-library-example-no-comments.lua}}
```

We've taken ideas from formats such as SFZ or DecentSampler but focus more on ease-of-use and extensibility, and bringing the power of a full programming language to ease developing complicated libraries.

A `floe.lua` file is only concerned with mapping and configuring audio-files. Sound-shaping parameters such as envelopes, filters and effects are offered by-default on Floe's GUI.

In other sample library formats such as SFZ or Kontakt, regions are arranged into 'groups'. Floe does not have this concept. Instead, the features of the Lua programming language (such as functions or loops) can be used to apply similar configuration to a set of regions. Additionally, Floe offers a function called `floe.extend_table(base_table, table)`, which allows new tables to be created that are based off an existing table. 

#### Library API
Floe provides an API (a set of functions) to `floe.lua` files. This API is available under a table called `floe`, for example, `floe.new_instrument(...)`. It features functions for creating a library, creating instruments, adding regions and impulse responses. At the end of your `floe.lua` file, you return the library object created with `floe.new_library(...)`.

Floe runs your `floe.lua` file with Lua version {{#include ../mdbook_config.txt:lua-version}}.

You have access to some of Lua's standard libraries: `math`, `string`, `table`, `utf8`. The other standard libraries are not accessible to Lua - including the `require` function. This is to minimise security risks.

##### `floe.new_library`
Creates a new library. It takes one parameter: a table of configuration and returns a new library object. You should only call this once.
```lua
{{#include ../sample-library-example.lua:new_library}}
```

The library is the top-level object. It contains all the instruments, regions, and impulse responses.

##### `floe.new_instrument`
Creates a new instrument on the library. It takes 2 parameters: the library object and a table of configuration. It returns a new instrument object. You can call this function multiple times to create multiple instruments.
```lua
{{#include ../sample-library-example.lua:new_instrument}}
```

An instrument is like a musical instrument. It is a sound-producing entity that consists of one or more samples (samples are specified in regions). Each library can have multiple instruments.

##### `floe.add_region`
Adds a region to an instrument. It takes 2 parameters: the instrument object and a table of configuration. You can call this function multiple times to create multiple regions. Doesn't return anything.
```lua
{{#include ../sample-library-example.lua:add_region}}
```

A region is a part of an instrument. It defines an audio file and the conditions under which it will be played. For example, you might have a region that plays the audio file `Piano_C3.flac` when the note C3 is played. Each instrument must have one or more regions.

##### `floe.add_ir`
Adds an reverb impulse response to the library. It takes 2 parameters: the library object and a table of configuration. You can call this function multiple times to create multiple impulse responses. Doesn't return anything.
```lua
{{#include ../sample-library-example.lua:add_ir}}
```

#### Support API
Floe provides some additional functions to help improve how you configure libraries.

##### `floe.extend_table`
Extends a table with another table, including all sub-tables. It takes 2 parameters: the base table and the table to extend it with. The base table is not modified. The extension table is modified and returned. It has all the keys of the base table plus all the keys of the extended table. If a key exists in both tables, the value from the extension table is used.
```lua
{{#include ../sample-library-example.lua:extend_table}}
```

