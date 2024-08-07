<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Floe Library Format

## Overview

Floe sample libraries are plain, open folders of audio files (FLAC or WAV). They are accompanied by a `floe.lua` file that describes how the samples are mapped and configured.

It is a new format, along the same lines as SFZ or DecentSampler but focusing on ease-of-use and extensibility, and bringing the power of a full programming language to ease developing complicated library configurations.

## Developer documentation

Sample libraries are configured using a file written in the Lua programming language[^MDATA] (version {{#include ../mdbook_config.txt:lua-version}}).

Let's start with a simple example of one of these `floe.lua` files:
```lua
{{#include ../sample-library-example-no-comments.lua}}
```

It's a simple format, but when needed, you have the power of a full programming language (variables, arrays, loops, functions, etc.) to easily create more complicated configurations.

Floe automatically scans for Lua files in its sample library folders (these are configurable in the settings). It looks for files called `floe.lua`, or files ending with `.floe.lua`; for example, `woodwind-textures.floe.lua`. Floe automatically detects when files are added, removed or changed, and will immediately apply the changes.

In a `floe.lua` file, you use a set of functions that Floe provides (`floe.<function_name>()`) to create a library, instruments, regions, and impulse responses. At the end of your `floe.lua` file, you return the library object.

Note this file is only concerned with mapping and configuring audio-files (unlike [SFZ](https://en.wikipedia.org/wiki/SFZ_(file_format)), for example). Sound shaping is provided by Floe's GUI.

Note there there is no 'group' structure, as is often found in other sample-mapping formats such as SFZ. Instead, we can create functions or use loops to apply similar configuration to a set of regions. Additionally, Floe offers a support function called floe.extend_table(base_table, table) which allows new tables to be created that are based off an existing table. 

Additional, a `floe.lua` file has access to some of Lua's standard libraries: `math`, `string`, `table`, `utf8`. The other standard libraries are not accessible to Lua - including the `require` function.

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
