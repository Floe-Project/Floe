<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Developing Libraries for Floe

This document explains the details of Floe's sample library format and how to develop libraries for it. Developing libraries for Floe does require some basic programming knowledge.

Be sure to read the [About Sample Libraries](../about/sample-libraries.md) page first.

## Why a new format?
No existing format met our requirements for Floe, which are:
- Libraries should be plain folders of audio files.
- Libraries should be portable across filesystems & operating systems.
- Libraries should be configured using a proper programming language to enable creating complex libraries in a maintainable way.
- The format should be extensible - allowing us to innovate in the field of sampling.
- The solution should easily integrate into Floe's codebase.

## Simple configuration
This format is designed for configuring audio files into playable instruments. It does not add any programming logic or custom GUIs. Floe has a ready-to-use GUI and lots of audio parameters.

## The structure

Let's look at the structure of a Floe sample library:

```
📂FrozenPlain - Slow/
├── 📄slow.floe.lua
├── 📄Licence.pdf
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
- **About**: A file that describes the library. Any file format. Information about a library is useful for when someone might not have Floe's GUI available. Use this document to explain what the library contains, website links and any other information that might be useful.
- **Library folder name**: The folder containing the `floe.lua` file should be named: "Developer - Library Name".
- **Subfolders**: Subfolders are up to the developer. We recommend 'Samples' for audio files and 'Images' for images. These can have any number of subfolders. Your `floe.lua` file can reference any file in these subfolders.

## The `floe.lua` file

The `floe.lua` file is the most important part of a library. It's a script that maps and configures the audio files into playable instruments, written in the Lua ==lua-version== programming language.

This file uses [Floe's Lua functions](library-lua-scripts.md) to create the library, create instruments, and add regions and impulse responses. It can reference any file in the library using relative paths.

Here's an example of a `floe.lua` file:
```lua
==sample-library-example-lua-no-comments==
```


`floe.new_library()` must be called and returned it at the end of the script. All other features are optional. When Floe runs your Lua file, it will show you any errors that occur along with a line number and a description.



## How to get started
For now, we'll assume that you have a folder of audio samples and know what notes they correspond to.

1. Create a new folder in one of Floe's [sample library folders](../about/sample-libraries.md#your-library-folders). We recommend naming it 'Author Name - Library Name'.
1. Create a file in that folder called `my-library.floe.lua`.
1. Create a subfolder called `Samples` and put your audio files in there.
1. Open the Lua file in your text editor. Sublime Text or Visual Studio Code are good choices.
1. Use the `floe.new_library()` function to create your library, filling in all the fields marked `[required]` in the [Floe's Lua reference](library-lua-scripts.md).
1. Use `floe.new_instrument()` to create an instrument, and then add regions to it using `floe.add_region()`.
1. At the end of the file, return the library object you just created.
1. Floe automatically scans its library folders and will tell you if there's any errors.


## Creating high-quality samples

### Levels
It's important to ensure your audio samples have the the right levels. This makes browsing and switching samples in Floe a consistent, nice experience. Additionally, Floe offers the ability to layer sounds together; this process is more ergonomic when instruments have similar volume levels.

[Signet](https://github.com/SamWindell/Signet) can be a useful tool for changing the levels of your samples.

When changing the volume levels of a realistic multi-sampled instrument, you probably don't want to normalise each sample individually because part of the character of the instrument is its volume variation. Instead, you should change the gain of the instrument _as a whole_. Signet has features for this. It also has features for proportionally moving levels towards a target level. This allows you to keep some of the character of an instrument while nudging it towards a more consistent level.

Here are some guidelines for levels:
- Each samples's peak level should be less than -3 dB.
- RMS levels for an instrument _as a whole_ should be around -18 dB. Play the instrument polyphonically and watch the RMS level. If the instrument is designed to be monophonic, then adjust for that.
- Impulse responses should have an RMS level of around -16 dB.
- The noise floor should be as low as possible: -60 dB is a good target. Use high-quality noise reduction tools to remove noise from your samples if you need to. Noise levels can quickly stack up with a multi-sampled instrument played polyphonically. Being too aggressive with algorithmic noise reduction can make your samples sound unnatural - so it's a balance.

### Sample rate, bit depth, and file format
Floe only supports FLAC and WAV files. We recommend using FLAC for your samples. It's lossless and can reduce the file size by 50% to 70%. Floe loads FLAC files very quickly.

We find 44.1 kHz and 16-bit is often a perfectly reasonable choice. 48 kHz and 24-bit might also be appropriate in certain cases.

### Volume envelopes of samples
Floe blurs the line between a sampler and a sample-based synthesizer. It has lots of parameters for manipulating the sound in real-time.

If your sample is a single continuous sound, then don't include a fade-in or fade-out in the sample. Floe has a GUI for volume envelopes that offer more control: they can be adjusted to any value, automated by the DAW, and they are independent of the playback speed of the sample. If you have a sample that is stretched across a keyboard range, it will be sped-up or slowed-down in order to be the correct pitch. If there's a volume fade then the speed of the fade will change depending on the pitch of the voice. This is not normally a desirable effect.

If your sound has important timbral variation over time, then don't cut that away. Only if the sound is a constant tone should you remove the fade in/out.
