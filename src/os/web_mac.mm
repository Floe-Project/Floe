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

ErrorCodeOr<void> HttpsGet(String url, Writer writer) {
    NSURL* nsurl = [NSURL URLWithString:StringToNSString(url)];
    NSURLRequest* request = [NSURLRequest requestWithURL:nsurl];
    NSURLSession* session = [NSURLSession sharedSession];

    __block ErrorCodeOr<void> result {};
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    if (semaphore == nullptr) Panic("Failed to create semaphore");

    @try {
        NSURLSessionDataTask* task =
            [session dataTaskWithRequest:request
                       completionHandler:^(NSData* data, NSURLResponse*, NSError* error) {
                         if (error) {
                             g_debug_log.Debug({}, "Error: {}", error.localizedDescription.UTF8String);
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
