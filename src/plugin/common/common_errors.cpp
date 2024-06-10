// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common_errors.hpp"

#include "foundation/foundation.hpp"

ErrorCodeCategory const& CommonErrorCodeType() {
    static constexpr ErrorCodeCategory k_cat = {
        .category_id = "FLN",
        .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
            String str {};
            switch ((CommonError)code.code) {
                case CommonError::FileFormatIsInvalid:
                    str =
                        "The file's data is not valid for this operation. It might just be it's not the right type of file for this operation. Alternatively, it could be that the file is corrupt somehow. Or, if you manually edited the file, you might have made a mistake.";
                    break;
                case CommonError::CurrentVersionTooOld:
                    str = "Your Floe version is too old for this operation. Update to the latest version.";
                    break;
                case CommonError::PluginHostError: str = "There's an unspecified error with the host."; break;
                case CommonError::NotFound: str = "The requested item was not found."; break;
            }
            return writer.WriteChars(str);
        }};
    return k_cat;
}
