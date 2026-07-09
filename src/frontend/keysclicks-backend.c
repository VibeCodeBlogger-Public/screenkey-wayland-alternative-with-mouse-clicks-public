// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger

#include "keysclicks-backend.h"

#include <json-glib/json-glib.h>
#include <string.h>

#ifndef KEYSCLICKS_BACKEND_PATH
#define KEYSCLICKS_BACKEND_PATH "/usr/libexec/keysclicks-input"
#endif

struct _KeysClicksBackend {
	KeysClicksEventFunc callback;
	gpointer user_data;

	GSubprocess *process;
	GDataInputStream *lines;
	GCancellable *cancellable;
};

KeysClicksBackend *
keysclicks_backend_new(KeysClicksEventFunc callback, gpointer user_data)
{
	KeysClicksBackend *self = g_new0(KeysClicksBackend, 1);
	self->callback = callback;
	self->user_data = user_data;
	return self;
}

// Resolve the backend executable: honour KEYSCLICKS_BACKEND for
// uninstalled/dev runs, otherwise use the path baked in at build time.
static const char *
backend_executable(void)
{
	const char *override = g_getenv("KEYSCLICKS_BACKEND");
	if (override != NULL && *override != '\0')
		return override;
	return KEYSCLICKS_BACKEND_PATH;
}

// Extract the pointer "speed:" value out of a COSMIC input-config (RON) blob,
// e.g. `acceleration: Some(( profile: None, speed: -0.81, ))`. Returns a
// newly-allocated, C-locale number string ("-0.81") or NULL when absent. Pure
// string handling so it is unit-tested (tests/unit/test_accel_speed.c).
char *
keysclicks_backend_parse_accel_speed(const char *content)
{
	if (content == NULL)
		return NULL;
	const char *p = strstr(content, "speed:");
	if (p == NULL)
		return NULL;
	p += strlen("speed:");
	char *end = NULL;
	double v = g_ascii_strtod(p, &end);
	if (end == p) // nothing parseable after "speed:"
		return NULL;
	// Format in the C locale ('.' separator, matching the backend's strtod) and
	// with %g so it stays a clean, short number ("-0.81", not a 17-digit tail).
	char buf[G_ASCII_DTOSTR_BUF_SIZE];
	g_ascii_formatd(buf, sizeof buf, "%g", v);
	return g_strdup(buf);
}

// Best-effort: the COSMIC compositor's global pointer acceleration speed, so the
// privileged backend can configure libinput identically. Without this the
// backend's deltas accelerate differently from the on-screen cursor and the
// click-flash calibration (a linear fit) cannot correct the drift. NULL when not
// a COSMIC session or the value is unset.
static char *
compositor_accel_speed(void)
{
	char *path = g_build_filename(g_get_user_config_dir(), "cosmic",
				      "com.system76.CosmicComp", "v1", "input_default",
				      NULL);
	char *content = NULL;
	gboolean ok = g_file_get_contents(path, &content, NULL, NULL);
	g_free(path);
	if (!ok)
		return NULL;
	char *speed = keysclicks_backend_parse_accel_speed(content);
	g_free(content);
	return speed;
}

static void
dispatch_json_line(KeysClicksBackend *self, const char *line)
{
	JsonParser *parser = json_parser_new();
	if (json_parser_load_from_data(parser, line, -1, NULL)) {
		JsonNode *root = json_parser_get_root(parser);
		if (root != NULL && JSON_NODE_HOLDS_OBJECT(root)) {
			JsonObject *obj = json_node_get_object(root);
			const char *type =
				json_object_get_string_member_with_default(obj, "type", "");
			gint64 code = json_object_get_int_member_with_default(obj, "code", 0);
			gboolean pressed = json_object_get_boolean_member_with_default(
				obj, "pressed", FALSE);

			if (g_strcmp0(type, "key") == 0) {
				self->callback(KEYSCLICKS_EVENT_KEY, (guint32)code, pressed,
					       0.0, 0.0, self->user_data);
			} else if (g_strcmp0(type, "button") == 0) {
				self->callback(KEYSCLICKS_EVENT_BUTTON, (guint32)code, pressed,
					       0.0, 0.0, self->user_data);
			} else if (g_strcmp0(type, "motion") == 0) {
				double dx = json_object_get_double_member_with_default(obj, "dx", 0.0);
				double dy = json_object_get_double_member_with_default(obj, "dy", 0.0);
				self->callback(KEYSCLICKS_EVENT_MOTION, 0, FALSE, dx, dy,
					       self->user_data);
			}
		}
	}
	g_object_unref(parser);
}

static void read_next_line(KeysClicksBackend *self);

static void
on_line_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
	KeysClicksBackend *self = user_data;
	GError *error = NULL;
	gsize length = 0;
	char *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(source),
							  result, &length, &error);

	if (error != NULL) {
		if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning("keysclicks: error reading backend output: %s",
				  error->message);
		g_clear_error(&error);
		return;
	}

	if (line == NULL) {
		// EOF: the backend exited (denied authentication, killed, ...).
		g_message("keysclicks: input backend stream ended");
		return;
	}

	dispatch_json_line(self, line);
	g_free(line);
	read_next_line(self);
}

static void
read_next_line(KeysClicksBackend *self)
{
	g_data_input_stream_read_line_async(self->lines, G_PRIORITY_DEFAULT,
					    self->cancellable, on_line_ready, self);
}

gboolean
keysclicks_backend_start(KeysClicksBackend *self, GError **error)
{
	const char *executable = backend_executable();

	// Pass the compositor's pointer accel speed to the backend so it configures
	// libinput to match the cursor. NULL when unknown -> the varargs list simply
	// ends at the executable and the backend keeps libinput's default.
	char *speed = compositor_accel_speed();

	// KEYSCLICKS_BACKEND_NO_PKEXEC: run the backend directly, without elevating
	// through pkexec. Used for headless/dev runs where the executable is a
	// pre-authorised binary or a fake event source (no polkit prompt).
	if (g_getenv("KEYSCLICKS_BACKEND_NO_PKEXEC") != NULL)
		self->process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE, error,
						 executable, speed, NULL);
	else
		self->process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE, error, "pkexec",
						 executable, speed, NULL);
	g_free(speed);
	if (self->process == NULL)
		return FALSE;

	GInputStream *stdout_pipe = g_subprocess_get_stdout_pipe(self->process);
	self->lines = g_data_input_stream_new(stdout_pipe);
	self->cancellable = g_cancellable_new();

	read_next_line(self);
	return TRUE;
}

void
keysclicks_backend_stop(KeysClicksBackend *self)
{
	if (self == NULL)
		return;

	if (self->cancellable != NULL)
		g_cancellable_cancel(self->cancellable);

	if (self->process != NULL)
		g_subprocess_force_exit(self->process);
}

void
keysclicks_backend_free(KeysClicksBackend *self)
{
	if (self == NULL)
		return;
	keysclicks_backend_stop(self);
	g_clear_object(&self->lines);
	g_clear_object(&self->process);
	g_clear_object(&self->cancellable);
	g_free(self);
}
