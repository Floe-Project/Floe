// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#define Rect MacRect
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wextra-semi"
#include <Foundation/Foundation.h>
#pragma clang diagnostic pop
#undef Rect

#include "utils/logger/logger.hpp"

#include "misc_mac.hpp"
#include "web.hpp"

ErrorCodeOr<void> HttpsGet(String url, Writer writer, RequestOptions options) {
    NSURL* nsurl = [NSURL URLWithString:StringToNSString(url)];
    NSURLRequest* request = [NSURLRequest requestWithURL:nsurl];

    NSURLSessionConfiguration* session_config = [NSURLSessionConfiguration defaultSessionConfiguration];
    session_config.timeoutIntervalForRequest = (f64)options.timeout_seconds;
    session_config.timeoutIntervalForResource = (f64)options.timeout_seconds;
    NSURLSession* session = [NSURLSession sessionWithConfiguration:session_config];

    __block ErrorCodeOr<void> result {};
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    if (semaphore == nullptr) Panic("Failed to create semaphore");

    @try {
        NSURLSessionDataTask* task =
            [session dataTaskWithRequest:request
                       completionHandler:^(NSData* data, NSURLResponse*, NSError* error) {
                         if (error) {
                             LogDebug({}, "Error: {}", error.localizedDescription.UTF8String);
                             result = ErrorCode {WebError::NetworkError};
                         } else {
                             auto const o = writer.WriteBytes({(u8 const*)data.bytes, data.length});
                             if (o.HasError()) result = o.Error();
                         }
                         dispatch_semaphore_signal(semaphore);
                       }];
        [task resume];
        dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
    } @catch (NSException* e) {
        return ErrorCode {WebError::NetworkError};
    }

    return result;
}

ErrorCodeOr<void> HttpsPost(String url,
                            String body,
                            Span<String> headers,
                            Optional<Writer> response_writer,
                            RequestOptions options) {
    NSURL* nsurl = [NSURL URLWithString:StringToNSString(url)];
    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:nsurl];
    [request setHTTPMethod:@"POST"];

    // Set request body
    NSData* request_body = [NSData dataWithBytes:body.data length:body.size];
    [request setHTTPBody:request_body];

    // Add custom headers
    for (auto const& header : headers) {
        NSString* header_string = StringToNSString(header);
        NSArray* components = [header_string componentsSeparatedByString:@": "];
        if (components.count == 2) [request setValue:components[1] forHTTPHeaderField:components[0]];
    }

    NSURLSessionConfiguration* session_config = [NSURLSessionConfiguration defaultSessionConfiguration];
    session_config.timeoutIntervalForRequest = (f64)options.timeout_seconds;
    session_config.timeoutIntervalForResource = (f64)options.timeout_seconds;
    NSURLSession* session = [NSURLSession sessionWithConfiguration:session_config];

    __block ErrorCodeOr<void> result {};
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    if (semaphore == nullptr) Panic("Failed to create semaphore");

    @try {
        NSURLSessionDataTask* task =
            [session dataTaskWithRequest:request
                       completionHandler:^(NSData* data, NSURLResponse*, NSError* error) {
                         if (error) {
                             LogDebug({}, "Error: {}", error.localizedDescription.UTF8String);
                             result = ErrorCode {WebError::NetworkError};
                         } else if (response_writer.HasValue() && data) {
                             auto const o = response_writer->WriteBytes({(u8 const*)data.bytes, data.length});
                             if (o.HasError()) result = o.Error();
                         } else {
                             result = k_success;
                         }
                         dispatch_semaphore_signal(semaphore);
                       }];
        [task resume];
        dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
    } @catch (NSException* e) {
        return ErrorCode {WebError::NetworkError};
    }

    return result;
}

void WebGlobalInit() {}
void WebGlobalCleanup() {}
