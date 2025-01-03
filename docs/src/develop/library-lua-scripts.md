<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Library Lua Scripts

This document describes the functions you can use in your sample library's [`floe.lua` script](develop-libraries.md#the-floelua-file) to create and configure the library and its instruments.

Floe runs your script using Lua v==lua-version==. You have access to some of [Lua's standard libraries](https://www.lua.org/manual/5.4/manual.html#6): `math`, `string`, `table` and `utf8`. The other standard libraries are not available - including the `require` function. This is to minimise security risks.

If there are any errors in your script, Floe will show them on the GUI along with a line number and a description of the problem.

## Library Functions
Use these functions to create your sample library. Take note of the `[required]` annotations - omitting these fields will cause an error. 


### `floe.new_library`
Creates a new library. It takes one parameter: a table of configuration. It returns a new library object. You should only create one library in your script. Return the library at the end of your script.

The library is the top-level object. It contains all the instruments, regions, and impulse responses.

```lua
==sample-library-example-lua:new_library==
```


### `floe.new_instrument`
Creates a new instrument on the library. It takes 2 parameters: the library object and a table of configuration. It returns a new instrument object. You can call this function multiple times to create multiple instruments.

An instrument is like a musical instrument. It is a sound-producing entity that consists of one or more samples (samples are specified in regions). Each library can have multiple instruments.

```lua
==sample-library-example-lua:new_instrument==
```



### `floe.add_region`
Adds a region to an instrument. It takes 2 parameters: the instrument object and a table of configuration. Doesn't return anything. You can call this function multiple times to create multiple regions. 

A region is a part of an instrument. It defines an audio file and the conditions under which it will be played. For example, you might have a region that plays the audio file `Piano_C3.flac` when the note C3 is played. Each instrument must have one or more regions.
```lua
==sample-library-example-lua:add_region==
```


### `floe.add_ir`
Adds an reverb impulse response to the library. It takes 2 parameters: the library object and a table of configuration. Doesn't return anything. You can call this function multiple times to create multiple impulse responses. 
```lua
==sample-library-example-lua:add_ir==
```


## Support Function
Floe provides some additional functions to make developing libraries easier.


### `floe.extend_table`
Extends a table with another table, including all sub-tables. It takes 2 parameters: the base table and the table to extend it with. The base table is not modified. The extension table is modified and returned. It has all the keys of the base table plus all the keys of the extended table. If a key exists in both tables, the value from the extension table is used.

Floe doesn't have the concept of 'groups' like other formats like SFZ or Kontakt have. Instead, this function offers a way to apply a similar configuration to multiple regions. Alternatively, you can use functions and loops in Lua to add regions in a more dynamic way.

```lua
==sample-library-example-lua:extend_table==
```

