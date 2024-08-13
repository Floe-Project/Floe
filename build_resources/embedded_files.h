// SPDX-FileCopyrightText: 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char const* data;
    uint64_t size;
} EmbeddedString;

typedef struct {
    uint8_t const* data;
    uint64_t size;
    EmbeddedString name;
    EmbeddedString legacy_name;
    EmbeddedString filename;
} BinaryData;

enum {
    EmbeddedIr_Cold,
    EmbeddedIr_Smooth,
    EmbeddedIr_Cathedral,
    EmbeddedIr_Subtle,
    EmbeddedIr_Count,
};

typedef struct {
    BinaryData irs[EmbeddedIr_Count];
} EmbeddedIrData;

BinaryData EmbeddedFontAwesome();
BinaryData EmbeddedMada();
BinaryData EmbeddedRoboto();
BinaryData EmbeddedFiraSans();
BinaryData EmbeddedDefaultBackground();

EmbeddedIrData EmbeddedIrs();

#ifdef __cplusplus
}
#endif
