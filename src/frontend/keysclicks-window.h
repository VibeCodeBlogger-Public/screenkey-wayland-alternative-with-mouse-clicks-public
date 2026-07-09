// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger

#pragma once

#include <adwaita.h>

#include "keysclicks-privacy.h"
#include "keysclicks-settings.h"

G_BEGIN_DECLS

// Builds the settings/control-panel window. Every control is bound to the shared
// settings record: editing a control writes the value, persists it and notifies
// listeners so the live overlay updates at once. Returns the window (owned by
// the application).
GtkWidget *keysclicks_window_new(GtkApplication *app, KeysClicksSettings *settings,
				 KeysClicksPrivacy *privacy);

G_END_DECLS
