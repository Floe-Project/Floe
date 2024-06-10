// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

enum class CommonError {
    FileFormatIsInvalid,
    PluginHostError,
    CurrentVersionTooOld,
    NotFound,
};

ErrorCodeCategory const& CommonErrorCodeType();
inline ErrorCodeCategory const& ErrorCategoryForEnum(CommonError) { return CommonErrorCodeType(); }
