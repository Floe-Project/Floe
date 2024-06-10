<!--
SPDX-FileCopyrightText: 2018-2024 Sam Windell

SPDX-License-Identifier: CC0-1.0
-->

This folder contains code from Vital by Matt Tytel, released under the GPLv3 (or later) licence. It remains under the same licence. See the LICENCES folder in the root of this project for the full licence text.

It has been modified by Sam Windell, April 2024:
- Most files that don't depend on the effects have been removed; including JUCE and the build system.
- Some files have had some minor changes to make them compile in this new environment. The algorithms are unchanged.
- The files have been made to follow the [REUSE](https://reuse.software/) specification to ensure the parent project remains compliant. Each file now has a SPDX license identifier and copyright notice at the top. The full license text is available in the LICENCES folder.
- A wrapper has been added to provide a more friendly API to use the effects as a library.

