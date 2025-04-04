// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cli_arg_parse.hpp"

ErrorCodeCategory const g_cli_error_code_category = {
    .category_id = "CL",
    .message =
        [](Writer const& writer, ErrorCode e) {
            return writer.WriteChars(({
                String s {};
                switch ((CliError)e.code) {
                    case CliError::InvalidArguments: s = "invalid arguments"; break;
                    case CliError::HelpRequested: s = "help requested"; break;
                    case CliError::VersionRequested: s = "version requested"; break;
                }
                s;
            }));
        },
};
