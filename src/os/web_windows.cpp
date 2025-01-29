// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <windows.h>
#include <winhttp.h>

//
#include "os/undef_windows_macros.h"

#include "misc.hpp"
#include "web.hpp"

ErrorCodeOr<void> HttpsGet(String url, Writer response_writer, RequestOptions options) {
    ArenaAllocatorWithInlineStorage<1000> temp_arena {Malloc::Instance()};

    URL_COMPONENTS url_comps {};
    url_comps.dwStructSize = sizeof(URL_COMPONENTS);
    url_comps.dwHostNameLength = (DWORD)-1;
    url_comps.dwUrlPathLength = (DWORD)-1;
    auto wide_url = *Widen(temp_arena, url);
    if (!WinHttpCrackUrl(wide_url.data, (DWORD)wide_url.size, 0, &url_comps))
        return ErrorCode {WebError::ApiError};

    ASSERT(url_comps.lpszHostName);
    ASSERT(url_comps.dwHostNameLength);
    ASSERT(url_comps.lpszUrlPath);
    ASSERT(url_comps.dwUrlPathLength);

    auto const server = NullTerminated({url_comps.lpszHostName, url_comps.dwHostNameLength}, temp_arena);
    auto const path = NullTerminated({url_comps.lpszUrlPath, url_comps.dwUrlPathLength}, temp_arena);

    auto session =
        WinHttpOpen(L"Floe", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) return ErrorCode {WebError::NetworkError};
    DEFER { WinHttpCloseHandle(session); };

    auto const timeout_ms = int(options.timeout_seconds * 1000);
    WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    unsigned long protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    auto connection = WinHttpConnect(session, server, INTERNET_DEFAULT_PORT, 0);
    if (connection == nullptr) return ErrorCode {WebError::NetworkError};
    DEFER { WinHttpCloseHandle(connection); };

    auto request = WinHttpOpenRequest(connection,
                                      L"GET",
                                      path,
                                      nullptr,
                                      WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      WINHTTP_FLAG_SECURE);
    if (!request) return ErrorCode {WebError::NetworkError};
    DEFER { WinHttpCloseHandle(request); };

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        return ErrorCode {WebError::NetworkError};

    if (!WinHttpReceiveResponse(request, nullptr)) return ErrorCode {WebError::NetworkError};

    DWORD bytes_available = 0;
    DynamicArray<u8> out_buffer {PageAllocator::Instance()};
    do {
        bytes_available = 0;
        if (WinHttpQueryDataAvailable(request, &bytes_available)) {
            dyn::Resize(out_buffer, bytes_available);
            DWORD bytes_read = 0;
            if (!WinHttpReadData(request, (LPVOID)(out_buffer.data), bytes_available, &bytes_read))
                return ErrorCode {WebError::NetworkError};
            TRY(response_writer.WriteBytes({out_buffer.data, bytes_read}));
        } else {
            return ErrorCode {WebError::NetworkError};
        }
    } while (bytes_available > 0);

    return k_success;
}

ErrorCodeOr<void> HttpsPost(String url,
                            String body,
                            Span<String> headers,
                            Optional<Writer> response_writer,
                            RequestOptions options) {
    ArenaAllocatorWithInlineStorage<1000> temp_arena {Malloc::Instance()};
    URL_COMPONENTS url_comps {};
    url_comps.dwStructSize = sizeof(URL_COMPONENTS);
    url_comps.dwHostNameLength = (DWORD)-1;
    url_comps.dwUrlPathLength = (DWORD)-1;

    auto wide_url = *Widen(temp_arena, url);
    if (!WinHttpCrackUrl(wide_url.data, (DWORD)wide_url.size, 0, &url_comps))
        return ErrorCode {WebError::ApiError};

    ASSERT(url_comps.lpszHostName);
    ASSERT(url_comps.dwHostNameLength);
    ASSERT(url_comps.lpszUrlPath);
    ASSERT(url_comps.dwUrlPathLength);

    auto const server = NullTerminated({url_comps.lpszHostName, url_comps.dwHostNameLength}, temp_arena);
    auto const path = NullTerminated({url_comps.lpszUrlPath, url_comps.dwUrlPathLength}, temp_arena);

    auto session =
        WinHttpOpen(L"Floe", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) return ErrorCode {WebError::NetworkError};
    DEFER { WinHttpCloseHandle(session); };

    auto const timeout_ms = int(options.timeout_seconds * 1000);
    WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    unsigned long protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    auto connection = WinHttpConnect(session, server, INTERNET_DEFAULT_PORT, 0);
    if (connection == nullptr) return ErrorCode {WebError::NetworkError};
    DEFER { WinHttpCloseHandle(connection); };

    auto request = WinHttpOpenRequest(connection,
                                      L"POST",
                                      path,
                                      nullptr,
                                      WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      WINHTTP_FLAG_SECURE);
    if (!request) return ErrorCode {WebError::NetworkError};
    DEFER { WinHttpCloseHandle(request); };

    // Add custom headers
    for (auto const& header : headers) {
        auto wide_header = *Widen(temp_arena, header);
        if (!WinHttpAddRequestHeaders(request,
                                      wide_header.data,
                                      (DWORD)wide_header.size,
                                      WINHTTP_ADDREQ_FLAG_ADD)) {
            return ErrorCode {WebError::ApiError};
        }
    }

    // Send request with body
    if (!WinHttpSendRequest(request,
                            WINHTTP_NO_ADDITIONAL_HEADERS,
                            0,
                            (LPVOID)body.data,
                            (DWORD)body.size,
                            (DWORD)body.size,
                            0)) {
        return ErrorCode {WebError::NetworkError};
    }

    if (!WinHttpReceiveResponse(request, nullptr)) return ErrorCode {WebError::NetworkError};

    // Only process response if writer is provided
    if (response_writer.HasValue()) {
        DWORD bytes_available = 0;
        DynamicArray<u8> out_buffer {PageAllocator::Instance()};
        do {
            bytes_available = 0;
            if (WinHttpQueryDataAvailable(request, &bytes_available)) {
                dyn::Resize(out_buffer, bytes_available);
                DWORD bytes_read = 0;
                if (!WinHttpReadData(request, (LPVOID)(out_buffer.data), bytes_available, &bytes_read))
                    return ErrorCode {WebError::NetworkError};
                TRY(response_writer->WriteBytes({out_buffer.data, bytes_read}));
            } else {
                return ErrorCode {WebError::NetworkError};
            }
        } while (bytes_available > 0);
    }

    return k_success;
}

void WebGlobalInit() {}
void WebGlobalCleanup() {}
