<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Floe Packages

Floe Packages are ZIP files that contain [Floe sample libraries](../about/sample-libraries.md) and/or presets. These are the files that users download to install new libraries and presets into Floe.

Floe offers an easy-to-use GUI for [installing these Packages](../installation/installing-libraries-and-presets.md). This installation process carefully considers the user's existing libraries and presets, their versions, their installation preferences, even whether their installed libraries have been modified or not. The result is something that should 'just work' or at least provide clear instructions on what to do next.

As with Floe's sample library format, openness is key. That's why Floe Packages are just normal ZIP files with a specific structure. Anyone can create them and anyone can open them. Additionally, it gives the user the option to extract them manually rather than use Floe's GUI if they wish. 

You can create Floe Packages with any ZIP program, but if you can, we recommend using the command-line tool that we provide.

## Structure

A Floe Package is a ZIP file with the ending `.floe.zip`. It must contain either a subfolder called `Libraries` containing [sample libraries](../about/sample-libraries.md), or a subfolder called `Presets` containing preset folders, or both. 

### Single library + factory presets
Floe Packages typically contain a single library and a set of factory presets.
```
📦FrozenPlain - Arctic Strings.floe.zip/
├── 📁Libraries/
│   └── 📁Arctic Strings/
│       ├── 📄arctic-strings.floe.lua
│       ├── 📁Samples/
│       │   ├── 📄strings_c4.flac
│       │   └── 📄strings_d4.flac
│       └── 📁Images/
│           ├── 📄background.png
│           └── 📄icon.png
└── 📁Presets/
    └── 📁Arctic Strings Factory/
        ├── 📁Realistic/
        │   ├── 📄Octaved.floe-preset
        │   └── 📄Soft.floe-preset
        └── 📁Synthetic/
            ├── 📄Bright.floe-preset
            └── 📄Warm.floe-preset
```

### Bundle of libraries
Floe Packages can contain multiple libraries or presets.
```
📦Audioata - Synthwave Bundle.floe.zip/
├── 📁Libraries/
│   ├── 📁Synthwave Bass/
│   │   ├── 📄synthwave-bass.floe.lua
│   │   └── 📁Samples/
│   │       ├── 📄bass_c1.flac
│   │       └── 📄bass_d1.flac
│   ├── 📁Synthwave Drums/
│   │   ├── 📄synthwave-drums.floe.lua
│   │   └── 📁Samples/
│   │       ├── 📄kick.flac
│   │       └── 📄snare.flac
│   └── 📁Synthwave Synths/
│       ├── 📄synthwave-synths.floe.lua
│       └── 📁Samples/
│           ├── 📄synth_c4.flac
│           └── 📄synth_d4.flac
└── 📁Presets/
    └── 📁Synthwave Factory/
        ├── 📄Clean.floe-preset
        ├── 📄Dirty.floe-preset
        ├── 📄Big.floe-preset
        ├── 📄Small.floe-preset
        ├── 📄Bright.floe-preset
        └── 📄Warm.floe-preset
```

### Floe packager tool
Floe Packages can be created using any ZIP tool. But if possible, we recommend using our packager command-line tool. It adds a few things to the Floe Package that provide a slightly better experience for the user when installing your packages with Floe's GUI.
