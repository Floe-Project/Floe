// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#include <curl/curl.h>

#include "utils/logger/logger.hpp"

#include "misc.hpp"
#include "web.hpp"

size_t WriteFunction(void* ptr, size_t size, size_t nmemb, void* data) {
    auto& writer = *(Writer*)data;
    auto _ = writer.WriteBytes({(u8 const*)ptr, size * nmemb});
    return size * nmemb;
}

ErrorCodeOr<void> HttpsGet(String url, Writer writer) {
    // TODO: this shouldn't happen every call
    curl_global_init(CURL_GLOBAL_DEFAULT);
    DEFER { curl_global_cleanup(); };

    auto curl = curl_easy_init();
    if (!curl) return ErrorCode {WebError::ApiError};
    DEFER { curl_easy_cleanup(curl); };

    ArenaAllocatorWithInlineStorage<1000> arena {Malloc::Instance()};

    curl_easy_setopt(curl, CURLOPT_URL, NullTerminated(url, arena));
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.42.0");

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFunction);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &writer);

    char error_buffer[CURL_ERROR_SIZE] {};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    auto const return_code = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        g_debug_log.Debug({},
                          "Reponse is non-200: {}, error: {}",
                          http_code,
                          FromNullTerminated(error_buffer));
        return ErrorCode {WebError::Non200Response};
    }
    if (return_code != CURLE_OK) {
        g_debug_log.Debug({}, "CURL ERROR: {}, {}", (int)return_code, FromNullTerminated(error_buffer));
        return ErrorCode {WebError::ApiError};
    }

    return k_success;
}
