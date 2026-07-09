// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger
//
// Dark-green identity + responsive-hover styling for the settings window, kept in
// ONE pure (no-GTK) place so a headless unit test (test_accent_css) can assert that
// every interactive block has a hover rule — and that the identity colours survive —
// without linking GTK/Adwaita.
#pragma once

static inline const char *
keysclicks_accent_css(void)
{
	return
		"@define-color accent_color #16a34a;\n"    // foreground green (text/links)
		"@define-color accent_bg_color #002B00;\n" // deep green fill
		"@define-color accent_fg_color #ffffff;\n"
		"button.suggested-action {\n"
		"  background-color: #002B00;\n"
		"  color: #ffffff;\n"
		"  border: 1px solid #15803d;\n"
		"}\n"
		// Switch: deep-green fill, NO outline (owner preference).
		"switch:checked {\n"
		"  background-color: #002B00;\n"
		"  border: none;\n"
		"}\n"
		// Dark-green frame around the settings window.
		".keysclicks-settings {\n"
		"  border: 2px solid #15803d;\n"
		"  border-radius: 13px;\n"
		"}\n"
		// Responsive hover: EVERY settings row (action / switch / combo / spin) lightens
		// slightly on hover, so all blocks react consistently (owner likes this cue).
		".keysclicks-settings row:hover {\n"
		"  background-color: rgba(255, 255, 255, 0.06);\n"
		"}\n"
		// The +/- buttons and the number pill share the SAME height and vertical inset
		// so the editable field lines up with the buttons instead of towering over them.
		".keysclicks-settings spinbutton > text,\n"
		".keysclicks-settings spinbutton button {\n"
		"  min-height: 24px;\n"
		"  margin-top: 7px;\n"
		"  margin-bottom: 7px;\n"
		"}\n"
		".keysclicks-settings spinbutton {\n"
		"  background: none;\n"
		"}\n"
		// Small grey pill only around the NUMBER (the spin button's text node): grey =
		// editable, lighter on hover (responsive), deep-green while being edited.
		".keysclicks-settings spinbutton > text {\n"
		"  background-color: rgba(255, 255, 255, 0.12);\n"
		"  border-radius: 8px;\n"
		"  margin-left: 4px;\n"
		"  margin-right: 2px;\n"
		"  padding: 0 6px;\n"
		"  min-width: 0;\n"
		"}\n"
		".keysclicks-settings spinbutton > text:hover {\n"
		"  background-color: rgba(255, 255, 255, 0.22);\n"
		"}\n"
		".keysclicks-settings spinbutton:focus-within > text {\n"
		"  background-color: #002B00;\n"
		"  color: #ffffff;\n"
		"}\n"
		// Small green "PRO" badge flagging the in-development automatic-hiding provider.
		".pro-badge {\n"
		"  background-color: #002B00;\n"
		"  color: #ffffff;\n"
		"  border: 1px solid #15803d;\n"
		"  border-radius: 8px;\n"
		"  padding: 1px 7px;\n"
		"  font-size: 11px;\n"
		"  font-weight: bold;\n"
		"}\n"
		// Kill the green focus OUTLINE everywhere in this window (libadwaita drew it in
		// the accent colour; it flashed on rows during the hide-keys chord / clicks).
		".keysclicks-settings *:focus,\n"
		".keysclicks-settings *:focus-visible,\n"
		".keysclicks-settings *:focus-within {\n"
		"  outline: none;\n"
		"}\n";
}
