// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger

#include "keysclicks-feedback-payload.h"

#include <json-glib/json-glib.h>

char *
keysclicks_feedback_build_payload(const char *type, const char *message,
				  const char *build, const char *viewport,
				  const char *screenshot_data_url)
{
	JsonBuilder *b = json_builder_new();
	json_builder_begin_object(b);

	// Tag the app so the endpoint can route keysclicks reports to their own place;
	// harmless (dropped) if the server hasn't whitelisted it yet.
	json_builder_set_member_name(b, "app");
	json_builder_add_string_value(b, "keysclicks");

	json_builder_set_member_name(b, "type");
	json_builder_add_string_value(b, g_strcmp0(type, "idea") == 0 ? "idea" : "bug");

	json_builder_set_member_name(b, "message");
	json_builder_add_string_value(b, message != NULL ? message : "");

	// A desktop app has no page URL/referrer; send stable placeholders so the schema
	// matches the existing web reporter.
	json_builder_set_member_name(b, "url");
	json_builder_add_string_value(b, "app://keysclicks");
	json_builder_set_member_name(b, "referrer");
	json_builder_add_string_value(b, "");

	json_builder_set_member_name(b, "viewport");
	json_builder_add_string_value(b, viewport != NULL ? viewport : "");

	json_builder_set_member_name(b, "buildStamp");
	json_builder_add_string_value(b, build != NULL ? build : "");

	// Honeypot: we are a real client, so always empty.
	json_builder_set_member_name(b, "website");
	json_builder_add_string_value(b, "");

	json_builder_set_member_name(b, "screenshot");
	if (screenshot_data_url != NULL && *screenshot_data_url != '\0')
		json_builder_add_string_value(b, screenshot_data_url);
	else
		json_builder_add_null_value(b);

	json_builder_end_object(b);

	JsonGenerator *gen = json_generator_new();
	JsonNode *root = json_builder_get_root(b);
	json_generator_set_root(gen, root);
	char *out = json_generator_to_data(gen, NULL);

	json_node_unref(root);
	g_object_unref(gen);
	g_object_unref(b);
	return out;
}
