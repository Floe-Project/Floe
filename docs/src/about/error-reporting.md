<!--
SPDX-FileCopyrightText: 2025 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Error Reporting

Floe strives to be reliable for professional work. To help us achieve this we have various systems in place.

## Error reports
If Floe detects that something has gone wrong, it will submit a totally anonymous error report to us. This report contains information about the error, the state of Floe leading up to the error, and information about the computer Floe is running on. 

This enables us to get the key information to fix bugs quickly without having to get you to manually gather and send us information.

You can disable all online reporting in the Preferences panel if you wish. Floe is open source, so you can also inspect the code to see how this works.

## Send feedback
Floe has a form that you can use to submit bug reports, feature requests, or general feedback. This is found by clicking on the 'Send Feedback' button in the three-dots icon <i class="fa fa-ellipsis-v"></i> menu at the top of Floe's window.

Please use this to report any issues you encounter. We want to know about it and want to fix it.

## Crash protection
Floe tries to never crash the host. If Floe detects that something has gone wrong, it will enter an unresponsive mode rather than crashing the host. It won't produce any sound and it can't be interacted with. In this rare situation, make note of the instance name at the top of Floe's window, such as 'dawn-205'. Then, remove the plugin from your DAW and re-add it. You can then restore your work from an [autosave](./autosave.md) with the instance name you just noted. Anonymous crash information will usually be reported to us automatically for us to address the issue.
