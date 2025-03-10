// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "foundation/foundation.hpp"

enum class WebError : u32 {
    ApiError,
    NetworkError,
    Non200Response,
    Count,
};

static constexpr ErrorCodeCategory k_web_error_category {
    .category_id = "FS",
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

PUBLIC ErrorCodeCategory const& ErrorCategoryForEnum(WebError) { return k_web_error_category; }

void WebGlobalInit();
void WebGlobalCleanup();

struct RequestOptions {
    Span<String const> headers;
    f32 timeout_seconds = 10.0f;
};

// blocking
ErrorCodeOr<void> HttpsGet(String url, Writer writer, RequestOptions options = {});
ErrorCodeOr<void>
HttpsPost(String url, String body, Optional<Writer> response_writer, RequestOptions options = {});
