<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Installing sample libraries

## Floe Package
A Floe Package is a zip file with the ending `.floe.zip`. It must contain either a subfolder called `Libraries` containing [sample libraries](../sample-library-format.md), or a subfolder called `Presets` containing preset folders, or both. 

Floe can install Floe Packages via the GUI.

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
