// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "web.hpp"

ErrorCodeCategory const g_web_error_category {
    .category_id = "WB",
    .message = [](Writer const& writer, ErrorCode e) -> ErrorCodeOr<void> {
        auto const get_str = [code = e.code]() -> String {
            switch ((WebError)code) {
                case WebError::ApiError: return "API error";
                case WebError::NetworkError: return "network error";
                case WebError::Non200Response: return "non-200 response";
                case WebError::Count: break;
            }
            return "";
        };
        return writer.WriteChars(get_str());
    },
};
