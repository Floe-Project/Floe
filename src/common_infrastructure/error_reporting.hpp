// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sentry/sentry.hpp"

// not thread-safe, call once near the start of the program
void InitBackgroundErrorReporting(Span<sentry::Tag const> tags);

// not thread-safe, call near the end of the program
void RequestBackgroundErrorReportingEnd(); // begins ending the thread
void WaitForBackgroundErrorReportingEnd(); // waits for thread to end

// thread-safe, not signal-safe, works even if InitErrorReporting() was not called
void ReportError(sentry::Error&& error);
