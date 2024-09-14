<!--
Copyright 2018-2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
--->
- `foundation`: basics, designed to be used by every single file
- `os`: OS API abstractions: filesystem, threading, etc.
- `utils`: more specialised stuff building off of foundation + os
- `tests`: unit tests and framework
- `plugin`: audio plugin code
- `standalone_wrapper`: audio plugin standalone wrapper
- `windows_installer`: custom win32 installer
- `gen_docs_tool`: tiny cli util to generate things for the docs
- `common_infrastructure`: floe-specific infrastructure
- `packager`: tool to package up libraries/presets for distribution
