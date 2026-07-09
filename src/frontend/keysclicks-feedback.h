// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger
#pragma once

#include <gtk/gtk.h>

#include "keysclicks-settings.h"

// A ready-made header button (Ionicons "bug-outline" icon) that opens the feedback
// dialog. Placement mirrors the maintainer's other apps (top-right).
GtkWidget *keysclicks_feedback_button_new(KeysClicksSettings *settings);

// Open the "Report a bug or idea" dialog, modal over `parent`.
void keysclicks_feedback_present(GtkWindow *parent, KeysClicksSettings *settings);
