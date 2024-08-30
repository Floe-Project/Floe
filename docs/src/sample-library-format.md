<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Floe Library Format

## Overview

Floe sample libraries are plain, open folders of audio files (FLAC or WAV). They are accompanied by a `floe.lua` file that describes how the samples are mapped and configured.

It is a new format, along the same lines as SFZ or DecentSampler but focusing on ease-of-use and extensibility, and bringing the power of a full programming language to ease developing complicated library configurations.

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

## Developer documentation

Sample libraries are configured using a file written in the Lua programming language[^MDATA] (version {{#include ../mdbook_config.txt:lua-version}}).

Let's start with a simple example of a `floe.lua` file:
```lua
{{#include ../sample-library-example-no-comments.lua}}
```

Floe automatically scans for Lua files in its sample library folders (these are configurable in the settings). It looks for files called `floe.lua`, or files ending with `.floe.lua`; for example, `woodwind-textures.floe.lua`. Floe automatically detects when files are added, removed or changed, and will immediately apply the changes.

Floe provides an API (a set of functions) to `floe.lua` files. This API is available under a table called `floe`, for example, `floe.new_instrument(...)`. It features functions for creating a library, creating instruments, adding regions and impulse responses. At the end of your `floe.lua` file, you return the library object created with `floe.new_library(...)`.

A `floe.lua` file is only concerned with mapping and configuring audio-files. Sound-shaping parameters such as envelopes, filters and effects are offered by-default on Floe's GUI.

In other sample library formats such as SFZ or Kontakt, regions are arranged into 'groups'. Floe does not have this concept. Instead, the features of the Lua programming language (such as functions or loops) can be used to apply similar configuration to a set of regions. Additionally, Floe offers a function called `floe.extend_table(base_table, table)`, which allows new tables to be created that are based off an existing table. 

Additionally, a `floe.lua` file has access to some of Lua's standard libraries: `math`, `string`, `table`, `utf8`. The other standard libraries are not accessible to Lua - including the `require` function. This is to minimise security risks.

## Core Functions

### `floe.new_library`
Creates a new library. It takes one parameter: a table of configuration and returns a new library object. You should only call this once.
```lua
{{#include ../sample-library-example.lua:new_library}}
```

The library is the top-level object. It contains all the instruments, regions, and impulse responses.

### `floe.new_instrument`
Creates a new instrument on the library. It takes 2 parameters: the library object and a table of configuration. It returns a new instrument object. You can call this function multiple times to create multiple instruments.
```lua
{{#include ../sample-library-example.lua:new_instrument}}
```

An instrument is like a musical instrument. It is a sound-producing entity that consists of one or more samples (samples are specified in regions). Each library can have multiple instruments.

### `floe.add_region`
Adds a region to an instrument. It takes 2 parameters: the instrument object and a table of configuration. You can call this function multiple times to create multiple regions. Doesn't return anything.
```lua
{{#include ../sample-library-example.lua:add_region}}
```

A region is a part of an instrument. It defines an audio file and the conditions under which it will be played. For example, you might have a region that plays the audio file `Piano_C3.flac` when the note C3 is played. Each instrument must have one or more regions.

### `floe.add_ir`
Adds an reverb impulse response to the library. It takes 2 parameters: the library object and a table of configuration. You can call this function multiple times to create multiple impulse responses. Doesn't return anything.
```lua
{{#include ../sample-library-example.lua:add_ir}}
```

## Support Functions
### `floe.extend_table`
Extends a table with another table, including all sub-tables. It takes 2 parameters: the base table and the table to extend it with. The base table is not modified. The extension table is modified and returned. It has all the keys of the base table plus all the keys of the extended table. If a key exists in both tables, the value from the extension table is used.
```lua
{{#include ../sample-library-example.lua:extend_table}}
```

[^MDATA]: Floe also supports libraries in the MDATA format, but this is only for backwards compatibility with [Mirage](./mirage.md).
