<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Lua Sample Library Format

## Overview
Sample libraries in Floe are created using the Lua programming language (version {{#include ../mdbook_config.txt:lua-version}}). Lua is a simple, lightweight language that's used in many applications. 

Let's start with a simple example `config.lua` sample library configuration file.
```lua
{{#include ../sample-library-example-no-comments.lua}}
```

As you can see, a `config.lua` can be very simple, almost like a JSON, XML or YAML file. But when needed, you have the power of a full programming language to easily create more complicated configurations - you can use arrays, loops, functions, etc.

Unlike something like [SFZ](https://en.wikipedia.org/wiki/SFZ_(file_format)), this format is only concerned with mapping and configuring audio-files. Sound shaping is provided by Floe's GUI.

## How it works
Floe automatically scans for `config.lua` files in your sample library folders and re-reads them whenever they change. To instruct Floe what to do with this file, you must use a set of functions that Floe provides. These are available as `floe.<name>` and are described in detail below.

## `floe.new_library`
Creates a new library. It takes one parameter: a table of configuration and returns a new library object. You should only call this once.
```lua
{{#include ../sample-library-example.lua:new_library}}
```

## `floe.new_instrument`
Creates a new instrument on the library. It takes 2 parameters: the library object and a table of configuration. It returns a new instrument object. You can call this function multiple times to create multiple instruments.
```lua
{{#include ../sample-library-example.lua:new_instrument}}
```

## `floe.add_region`
Adds a region to an instrument. It takes 2 parameters: the instrument object and a table of configuration. You can call this function multiple times to create multiple regions. Doesn't return anything.
```lua
{{#include ../sample-library-example.lua:add_region}}
```

## `floe.add_ir`
Adds an reverb impulse response to the library. It takes 2 parameters: the library object and a table of configuration. You can call this function multiple times to create multiple impulse responses. Doesn't return anything.
```lua
{{#include ../sample-library-example.lua:add_ir}}
```

