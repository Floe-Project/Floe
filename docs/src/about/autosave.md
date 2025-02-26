<!--
SPDX-FileCopyrightText: 2025 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Autosave

Floe reduces the chance of you losing your work by automatically saving its state frequently. 

This is useful if either Floe or the DAW crashes unexpectedly. We strive to make Floe reliable for professional work. 

Every instance of Floe has an auto-generated name, for example: dawn-205. You can see this in the top panel of Floe's window. This is useful because it helps identify which instance of Floe an autosave came from so that you can correctly restore it. Autosave files also contain the date and time they were created.

The autosave feature has reasonable default settings, but you can edit them in the Preferences panel too.

Autosaves are just preset files. You can load them as you would a preset file. Autosaves can be found here:
- Windows: `C:\Users\Public\Floe\Autosaves`
- macOS: `/Users/Shared/Floe/Autosaves`

Some additional things to note:
- The autosave system is efficient - it shouldn't noticeably slow your computer down.
- Floe will automatically delete autosaves older than a given number of days.
- Floe will only keep a certain number of autosaves per instance. If the limit is reached, the oldest autosave will be deleted to make room for the new one.
- Autosaves are tiny files, typically around 2 Kb in size. Your computer won't run out of space because of them.
