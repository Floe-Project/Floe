<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Sample Libraries

View your installed libraries by clicking the <i class="fa fa-info-circle"></i> info button on Floe's main window.

## Open format

Floe uses a custom sample library format which supports the following features: 
- Velocity layers
- Round-robin
- Crossfade layers
- Loop points with crossfade 
- Convolution reverb IRs
- Velocity layer feathering
- Trigger samples on note-off

It's an open format consisting of a folder of audio files and a file in the [Lua](https://en.wikipedia.org/wiki/Lua_(programming_language)) programming language called `floe.lua`. 

Access to the audio files gives you the freedom to use a library's sounds in other software too.

> Openness is a key goal of Floe sample libraries. 
> 
> There's no proprietary file formats. There's just FLAC, WAV and Lua. By using widely-used file formats we ensure the longevity of libraries.

Floe's sample library format is designed to be extended. We are planning to add new features to the format while always retaining backwards compatibility.

## Where to get libraries
<div class="warning">
We're working on providing more libraries for Floe. This page will be updated soon.
</div>

You can use libraries developed by others or you can [develop your own](../develop/develop-libraries.md). Developing your own libraries does require some basic programming knowledge.


