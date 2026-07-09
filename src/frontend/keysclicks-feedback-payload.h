// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger
#pragma once

#include <glib.h>

// Build the JSON body posted to the public report endpoint. Pure (json-glib only, no
// GTK/network) so it is unit-testable headless. Caller owns the returned string.
//
// type:               "bug" or "idea" (anything else is treated as "bug").
// message:            the user's free text (may be "" / NULL).
// build:              the running build stamp, e.g. "46.c857002".
// viewport:           screen size string, e.g. "3840x2160" (may be "" / NULL).
// screenshot_data_url: a "data:image/...;base64,…" string, or NULL/"" for none.
char *keysclicks_feedback_build_payload(const char *type, const char *message,
					const char *build, const char *viewport,
					const char *screenshot_data_url);
