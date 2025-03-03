<!--
SPDX-FileCopyrightText: 2025 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Packages

Sample libraries and presets are typically put into ZIP files called _Floe packages_. This makes them convenient for downloading and installing.

Floe can easily [extract and install these packages](./install-packages.md).

## Additional Information

Packages are just normal ZIP files, but they contain subfolders in a particular structure that Floe knows how to handle. 

Because they are just ZIP files, Floe is actually not required; you can unzip the package yourself and use the libraries for something unrelated to Floe if you wanted.

A package can contain any number of libraries and/or presets. Typically though, a package contains one library and a folder of factory presets for that library.

Just like the libraries and presets themselves, packages are portable - you can copy them to other computers or operating systems.
