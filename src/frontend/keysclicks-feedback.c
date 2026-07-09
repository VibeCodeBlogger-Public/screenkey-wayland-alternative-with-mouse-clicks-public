// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger
//
// In-app "Report a bug or idea" — mirrors the maintainer's approved web/RN widget
// (Ionicons bug-outline icon, top-right, Bug/Idea + free text + Send/Cancel), adapted
// to GTK. Privacy-first: nothing leaves the machine until Send, and the screenshot +
// diagnostics are per-report opt-ins (OFF by default). See docs/feedback-architecture.md.

#include "keysclicks-feedback.h"

#include "keysclicks-feedback-payload.h"
#include "version.h"

#include <curl/curl.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <string.h>

#define FEEDBACK_ENDPOINT "https://hackonvibe.com/api/report"
#define FEEDBACK_MAX_MESSAGE 2000
#define FEEDBACK_SHOT_MAX_W 1920

// Exact Ionicons "bug-outline" (viewBox 0 0 512 512), the icon already approved in the
// maintainer's other apps. Stroke is a neutral light grey so it reads on the dark
// header (the source apps use the same neutral treatment).
static const char BUG_SVG[] =
	"<svg viewBox=\"0 0 512 512\" xmlns=\"http://www.w3.org/2000/svg\">"
	"<path d=\"M370 378c28.89 23.52 46 46.07 46 86M142 378c-28.89 23.52-46 46.06-46 "
	"86M384 208c28.89-23.52 32-56.07 32-96M128 206c-28.89-23.52-32-54.06-32-94M464 "
	"288.13h-80M128 288.13H48M256 192v256\" fill=\"none\" stroke=\"#cdd0d6\" "
	"stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke-width=\"32\"/>"
	"<path d=\"M256 448h0c-70.4 0-128-57.6-128-128v-96.07c0-65.07 57.6-96 128-96h0c70.4 "
	"0 128 25.6 128 96V320c0 70.4-57.6 128-128 128z\" fill=\"none\" stroke=\"#cdd0d6\" "
	"stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke-width=\"32\"/>"
	"<path d=\"M179.43 143.52a49.08 49.08 0 01-3.43-15.73A80 80 0 01255.79 48h.42A80 80 "
	"0 01336 127.79a41.91 41.91 0 01-3.12 14.3\" fill=\"none\" stroke=\"#cdd0d6\" "
	"stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke-width=\"32\"/></svg>";

typedef struct {
	GtkWindow *window;
	KeysClicksSettings *settings;
	GtkWidget *bug_radio;
	GtkWidget *idea_radio;
	GtkTextView *text;
	GtkCheckButton *attach_shot;
	GtkWidget *shot_preview; // GtkImage, hidden until a capture lands
	GtkCheckButton *attach_diag;
	GtkWidget *send_btn;
	GtkWidget *status; // inline status / toast label
	char *screenshot_data_url; // captured JPEG data URL (owned) or NULL
	gboolean capturing;
	gboolean sending;
	gboolean closed; // set on destroy so async callbacks don't touch dead widgets
} Feedback;

typedef struct {
	char *data_url;
	GdkPixbuf *preview;
} CaptureResult;

typedef struct {
	gboolean transport_ok;
	gboolean ok;
	long http;
	char *ticket;
	char *server_err;
	char *err;
} SendResult;

// --- one-time libcurl init -------------------------------------------------

static void
ensure_curl(void)
{
	static gsize once = 0;
	if (g_once_init_enter(&once)) {
		curl_global_init(CURL_GLOBAL_DEFAULT);
		g_once_init_leave(&once, 1);
	}
}

// --- the bug icon ----------------------------------------------------------

static GtkWidget *
bug_image_new(int px)
{
	GBytes *bytes = g_bytes_new_static(BUG_SVG, sizeof(BUG_SVG) - 1);
	GInputStream *st = g_memory_input_stream_new_from_bytes(bytes);
	GdkPixbuf *pb = gdk_pixbuf_new_from_stream_at_scale(st, px, px, TRUE, NULL, NULL);
	g_object_unref(st);
	g_bytes_unref(bytes);
	if (pb == NULL) // no SVG loader on this box — fall back to a themed icon
		return gtk_image_new_from_icon_name("dialog-warning-symbolic");
	GdkTexture *tex = gdk_texture_new_for_pixbuf(pb);
	GtkWidget *img = gtk_image_new_from_paintable(GDK_PAINTABLE(tex));
	gtk_image_set_pixel_size(GTK_IMAGE(img), px);
	g_object_unref(pb);
	g_object_unref(tex);
	return img;
}

// --- small helpers ---------------------------------------------------------

static void
set_status(Feedback *fb, const char *msg)
{
	gtk_label_set_text(GTK_LABEL(fb->status), msg != NULL ? msg : "");
	gtk_widget_set_visible(fb->status, msg != NULL && *msg != '\0');
}

static char *
build_viewport(GtkWindow *parent)
{
	GdkDisplay *d = gdk_display_get_default();
	GdkMonitor *m = NULL;
	if (parent != NULL) {
		GdkSurface *surf = gtk_native_get_surface(GTK_NATIVE(parent));
		if (surf != NULL)
			m = gdk_display_get_monitor_at_surface(d, surf); // borrowed
	}
	GdkRectangle geo = { 0, 0, 0, 0 };
	if (m != NULL) {
		gdk_monitor_get_geometry(m, &geo);
	} else {
		GListModel *mons = gdk_display_get_monitors(d);
		if (g_list_model_get_n_items(mons) > 0) {
			GdkMonitor *mm = g_list_model_get_item(mons, 0);
			gdk_monitor_get_geometry(mm, &geo);
			g_object_unref(mm);
		}
	}
	return g_strdup_printf("%dx%d", geo.width, geo.height);
}

static char *
build_diagnostics(void)
{
	GString *s = g_string_new(NULL);
	char *os = g_get_os_info(G_OS_INFO_KEY_PRETTY_NAME);
	g_string_append_printf(s, "OS: %s\n", os != NULL ? os : "unknown");
	g_free(os);
	g_string_append_printf(s, "Build: v%s build %s\n", KEYSCLICKS_VERSION, KEYSCLICKS_BUILD); // i18n-ignore: diagnostics payload (English)
	g_string_append(s, "Monitors:"); // i18n-ignore: diagnostics payload (English)
	GListModel *mons = gdk_display_get_monitors(gdk_display_get_default());
	guint n = g_list_model_get_n_items(mons);
	for (guint i = 0; i < n; i++) {
		GdkMonitor *m = g_list_model_get_item(mons, i);
		GdkRectangle geo;
		gdk_monitor_get_geometry(m, &geo);
		const char *conn = gdk_monitor_get_connector(m);
		g_string_append_printf(s, " %s %dx%d@%.0f%%;", conn != NULL ? conn : "?",
				       geo.width, geo.height, gdk_monitor_get_scale(m) * 100.0);
		g_object_unref(m);
	}
	return g_string_free(s, FALSE);
}

// --- screenshot capture (worker thread) ------------------------------------

static char *
newest_png_in(const char *dir)
{
	GDir *d = g_dir_open(dir, 0, NULL);
	if (d == NULL)
		return NULL;
	char *found = NULL;
	const char *name;
	while ((name = g_dir_read_name(d)) != NULL) {
		if (g_str_has_suffix(name, ".png")) {
			g_free(found);
			found = g_build_filename(dir, name, NULL);
		}
	}
	g_dir_close(d);
	return found;
}

// Full-screen capture via cosmic-screenshot, downscaled + re-encoded to a JPEG data
// URL small enough to pass the endpoint's size cap. Best-effort: returns NULL on any
// failure and the report is simply sent without an image.
static char *
capture_screenshot_data_url(GdkPixbuf **preview_out)
{
	char *tmp = g_dir_make_tmp("keysclicks-shot-XXXXXX", NULL);
	if (tmp == NULL)
		return NULL;

	const char *argv[] = { "cosmic-screenshot", "--interactive=false",
			       "--notify=false", "--save-dir", tmp, NULL };
	GSubprocess *proc = g_subprocess_newv(
		argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL);
	char *out = NULL;
	if (proc != NULL) {
		char *sout = NULL;
		g_subprocess_communicate_utf8(proc, NULL, NULL, &sout, NULL, NULL);
		char *path = NULL;
		if (sout != NULL) {
			path = g_strstrip(g_strdup(sout));
			if (*path == '\0' || !g_file_test(path, G_FILE_TEST_EXISTS)) {
				g_free(path);
				path = NULL;
			}
			g_free(sout);
		}
		if (path == NULL)
			path = newest_png_in(tmp);

		if (path != NULL) {
			GdkPixbuf *pb = gdk_pixbuf_new_from_file(path, NULL);
			if (pb != NULL) {
				int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb);
				if (w > FEEDBACK_SHOT_MAX_W && w > 0) {
					int nh = (int)((double)h * FEEDBACK_SHOT_MAX_W / w);
					GdkPixbuf *sc = gdk_pixbuf_scale_simple(
						pb, FEEDBACK_SHOT_MAX_W, nh, GDK_INTERP_BILINEAR);
					g_object_unref(pb);
					pb = sc;
				}
				gchar *buf = NULL;
				gsize len = 0;
				if (pb != NULL &&
				    gdk_pixbuf_save_to_buffer(pb, &buf, &len, "jpeg", NULL,
							      "quality", "85", NULL)) {
					char *b64 = g_base64_encode((const guchar *)buf, len);
					out = g_strconcat("data:image/jpeg;base64,", b64, NULL);
					g_free(b64);
					if (preview_out != NULL && pb != NULL) {
						int pw = 360;
						int ph = (int)((double)gdk_pixbuf_get_height(pb) *
							       pw / gdk_pixbuf_get_width(pb));
						*preview_out = gdk_pixbuf_scale_simple(
							pb, pw, ph, GDK_INTERP_BILINEAR);
					}
				}
				g_free(buf);
				if (pb != NULL)
					g_object_unref(pb);
			}
			g_remove(path);
			g_free(path);
		}
		g_object_unref(proc);
	}
	g_rmdir(tmp);
	g_free(tmp);
	return out;
}

static void
capture_result_free(gpointer p)
{
	CaptureResult *r = p;
	if (r == NULL)
		return;
	g_free(r->data_url);
	if (r->preview != NULL)
		g_object_unref(r->preview);
	g_free(r);
}

static void
capture_thread(GTask *task, gpointer src, gpointer task_data, GCancellable *c)
{
	(void)src;
	(void)task_data;
	(void)c;
	GdkPixbuf *preview = NULL;
	char *url = capture_screenshot_data_url(&preview);
	if (url == NULL) {
		if (preview != NULL)
			g_object_unref(preview);
		g_task_return_pointer(task, NULL, NULL);
		return;
	}
	CaptureResult *r = g_new0(CaptureResult, 1);
	r->data_url = url;
	r->preview = preview;
	g_task_return_pointer(task, r, capture_result_free);
}

static void
capture_done(GObject *src, GAsyncResult *res, gpointer ud)
{
	(void)ud;
	GtkWindow *win = GTK_WINDOW(src);
	Feedback *fb = g_object_get_data(G_OBJECT(win), "feedback");
	CaptureResult *r = g_task_propagate_pointer(G_TASK(res), NULL);
	if (fb != NULL && !fb->closed) {
		fb->capturing = FALSE;
		gtk_widget_set_visible(GTK_WIDGET(fb->window), TRUE); // re-show after capture
		gtk_widget_set_sensitive(GTK_WIDGET(fb->attach_shot), TRUE);
		if (r != NULL && r->data_url != NULL) {
			g_free(fb->screenshot_data_url);
			fb->screenshot_data_url = g_strdup(r->data_url);
			if (r->preview != NULL) {
				GdkTexture *t = gdk_texture_new_for_pixbuf(r->preview);
				gtk_image_set_from_paintable(GTK_IMAGE(fb->shot_preview),
							     GDK_PAINTABLE(t));
				g_object_unref(t);
				gtk_widget_set_visible(fb->shot_preview, TRUE);
			}
			set_status(fb, _("Screenshot attached."));
		} else {
			// Capture failed: undo the tick and explain.
			gtk_check_button_set_active(fb->attach_shot, FALSE);
			set_status(fb, _("Screenshot unavailable — your description still helps."));
		}
	}
	capture_result_free(r);
}

// --- send (worker thread) --------------------------------------------------

static size_t
write_cb(char *ptr, size_t sz, size_t nm, void *ud)
{
	g_string_append_len((GString *)ud, ptr, sz * nm);
	return sz * nm;
}

static void
send_result_free(gpointer p)
{
	SendResult *r = p;
	if (r == NULL)
		return;
	g_free(r->ticket);
	g_free(r->server_err);
	g_free(r->err);
	g_free(r);
}

static void
send_thread(GTask *task, gpointer src, gpointer task_data, GCancellable *c)
{
	(void)src;
	(void)task_data;
	(void)c;
	const char *payload = g_task_get_task_data(task);
	SendResult *r = g_new0(SendResult, 1);
	GString *resp = g_string_new(NULL);
	CURL *curl = curl_easy_init();
	if (curl != NULL) {
		struct curl_slist *hdr =
			curl_slist_append(NULL, "Content-Type: application/json"); /* i18n-ignore: HTTP header */
		curl_easy_setopt(curl, CURLOPT_URL, FEEDBACK_ENDPOINT);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
		curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, payload);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "keysclicks/" KEYSCLICKS_VERSION);
		CURLcode rc = curl_easy_perform(curl);
		long code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
		r->http = code;
		r->transport_ok = (rc == CURLE_OK);
		if (rc != CURLE_OK)
			r->err = g_strdup(curl_easy_strerror(rc));
		if (rc == CURLE_OK && resp->len > 0) {
			JsonParser *p = json_parser_new();
			if (json_parser_load_from_data(p, resp->str, resp->len, NULL)) {
				JsonNode *root = json_parser_get_root(p);
				if (root != NULL && JSON_NODE_HOLDS_OBJECT(root)) {
					JsonObject *o = json_node_get_object(root);
					if (json_object_has_member(o, "ok"))
						r->ok = json_object_get_boolean_member(o, "ok");
					if (json_object_has_member(o, "ticketId"))
						r->ticket = g_strdup(json_object_get_string_member(
							o, "ticketId"));
					if (json_object_has_member(o, "error"))
						r->server_err = g_strdup(
							json_object_get_string_member(o, "error"));
				}
			}
			g_object_unref(p);
		}
		curl_slist_free_all(hdr);
		curl_easy_cleanup(curl);
	} else {
		r->err = g_strdup("curl init failed"); /* i18n-ignore: internal error, never shown */
	}
	g_string_free(resp, TRUE);
	g_task_return_pointer(task, r, send_result_free);
}

static gboolean
do_close(gpointer data)
{
	GtkWindow *w = data;
	Feedback *fb = g_object_get_data(G_OBJECT(w), "feedback");
	if (fb != NULL && !fb->closed)
		gtk_window_destroy(w);
	g_object_unref(w);
	return G_SOURCE_REMOVE;
}

static void
send_done(GObject *src, GAsyncResult *res, gpointer ud)
{
	(void)ud;
	GtkWindow *win = GTK_WINDOW(src);
	Feedback *fb = g_object_get_data(G_OBJECT(win), "feedback");
	SendResult *r = g_task_propagate_pointer(G_TASK(res), NULL);
	if (fb != NULL && !fb->closed) {
		fb->sending = FALSE;
		if (r != NULL && r->ok) {
			char *m;
			if (r->ticket != NULL) {
				char tk[9] = "";
				g_strlcpy(tk, r->ticket, sizeof(tk));
				m = g_strdup_printf(_("Report #%s sent — thank you!"), tk);
			} else {
				m = g_strdup(_("Report sent — thank you!"));
			}
			set_status(fb, m);
			g_free(m);
			gtk_widget_set_sensitive(fb->send_btn, FALSE);
			g_timeout_add(2500, do_close, g_object_ref(win));
		} else {
			gtk_widget_set_sensitive(fb->send_btn, TRUE);
			const char *msg = _("Could not send your report.");
			if (r != NULL && r->server_err != NULL)
				msg = r->server_err;
			else if (r != NULL && !r->transport_ok)
				msg = _("Network error — please try again.");
			set_status(fb, msg);
		}
	}
	send_result_free(r);
}

// --- UI handlers -----------------------------------------------------------

static void
on_attach_shot_toggled(GtkCheckButton *btn, gpointer ud)
{
	Feedback *fb = ud;
	if (!gtk_check_button_get_active(btn)) {
		set_status(fb, "");
		return;
	}
	// Only capture the screen when the user explicitly opts in (privacy-first). Hide
	// the dialog for the moment of capture so it is not itself in the shot, then the
	// callback re-shows it.
	if (fb->screenshot_data_url != NULL || fb->capturing)
		return; // already captured this session, or in flight
	fb->capturing = TRUE;
	set_status(fb, _("Capturing screenshot…"));
	gtk_widget_set_sensitive(GTK_WIDGET(fb->attach_shot), FALSE);
	gtk_widget_set_visible(GTK_WIDGET(fb->window), FALSE);
	GTask *task = g_task_new(G_OBJECT(fb->window), NULL, capture_done, NULL);
	g_task_run_in_thread(task, capture_thread);
	g_object_unref(task);
}

static void
on_cancel(GtkButton *b, gpointer ud)
{
	(void)b;
	Feedback *fb = ud;
	gtk_window_destroy(fb->window);
}

static void
on_send(GtkButton *b, gpointer ud)
{
	(void)b;
	Feedback *fb = ud;
	if (fb->sending)
		return;

	GtkTextBuffer *buf = gtk_text_view_get_buffer(fb->text);
	GtkTextIter s, e;
	gtk_text_buffer_get_bounds(buf, &s, &e);
	char *msg = gtk_text_buffer_get_text(buf, &s, &e, FALSE);
	if (g_utf8_strlen(msg, -1) > FEEDBACK_MAX_MESSAGE) {
		char *t = g_utf8_substring(msg, 0, FEEDBACK_MAX_MESSAGE);
		g_free(msg);
		msg = t;
	}
	if (gtk_check_button_get_active(fb->attach_diag)) {
		char *d = build_diagnostics();
		char *m2 = g_strconcat(msg, "\n\n--- diagnostics ---\n", d, NULL);
		g_free(msg);
		g_free(d);
		msg = m2;
	}

	const char *type =
		gtk_check_button_get_active(GTK_CHECK_BUTTON(fb->idea_radio)) ? "idea" : "bug";
	char *viewport = build_viewport(gtk_window_get_transient_for(fb->window));
	char *build = g_strdup_printf("v%s build %s", KEYSCLICKS_VERSION, KEYSCLICKS_BUILD); /* i18n-ignore: version stamp */
	const char *shot =
		gtk_check_button_get_active(fb->attach_shot) ? fb->screenshot_data_url : NULL;
	char *payload = keysclicks_feedback_build_payload(type, msg, build, viewport, shot);

	fb->sending = TRUE;
	gtk_widget_set_sensitive(fb->send_btn, FALSE);
	set_status(fb, _("Sending…"));

	GTask *task = g_task_new(G_OBJECT(fb->window), NULL, send_done, NULL);
	g_task_set_task_data(task, payload, g_free); // task owns payload
	g_task_run_in_thread(task, send_thread);
	g_object_unref(task);

	g_free(msg);
	g_free(viewport);
	g_free(build);
}

static void
on_dialog_destroy(GtkWidget *w, gpointer ud)
{
	(void)w;
	((Feedback *)ud)->closed = TRUE;
}

static void
feedback_free(gpointer p)
{
	Feedback *fb = p;
	g_free(fb->screenshot_data_url);
	g_free(fb);
}

// --- public API ------------------------------------------------------------

void
keysclicks_feedback_present(GtkWindow *parent, KeysClicksSettings *settings)
{
	ensure_curl();

	Feedback *fb = g_new0(Feedback, 1);
	fb->settings = settings;

	GtkWidget *win = gtk_window_new();
	fb->window = GTK_WINDOW(win);
	gtk_window_set_title(fb->window, _("Report a bug or idea"));
	gtk_window_set_modal(fb->window, TRUE);
	if (parent != NULL)
		gtk_window_set_transient_for(fb->window, parent);
	gtk_window_set_default_size(fb->window, 460, -1);
	gtk_widget_add_css_class(win, "keysclicks-settings"); // shared dark-green styling
	g_object_set_data_full(G_OBJECT(win), "feedback", fb, feedback_free);
	g_signal_connect(win, "destroy", G_CALLBACK(on_dialog_destroy), fb);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_top(box, 18);
	gtk_widget_set_margin_bottom(box, 16);
	gtk_widget_set_margin_start(box, 18);
	gtk_widget_set_margin_end(box, 18);
	gtk_window_set_child(fb->window, box);

	GtkWidget *title = gtk_label_new(_("Report a bug or idea"));
	gtk_widget_add_css_class(title, "title-3");
	gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
	gtk_box_append(GTK_BOX(box), title);

	// Bug / Idea radio pair.
	GtkWidget *types = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_widget_set_halign(types, GTK_ALIGN_CENTER);
	fb->bug_radio = gtk_check_button_new_with_label(_("🐞 Bug"));
	fb->idea_radio = gtk_check_button_new_with_label(_("💡 Idea"));
	gtk_check_button_set_group(GTK_CHECK_BUTTON(fb->idea_radio),
				   GTK_CHECK_BUTTON(fb->bug_radio));
	gtk_check_button_set_active(GTK_CHECK_BUTTON(fb->bug_radio), TRUE);
	gtk_box_append(GTK_BOX(types), fb->bug_radio);
	gtk_box_append(GTK_BOX(types), fb->idea_radio);
	gtk_box_append(GTK_BOX(box), types);

	GtkWidget *lbl = gtk_label_new(_("What happened? (optional)"));
	gtk_widget_set_halign(lbl, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(box), lbl);

	GtkWidget *scroller = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER,
				       GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request(scroller, -1, 96);
	gtk_widget_add_css_class(scroller, "card");
	fb->text = GTK_TEXT_VIEW(gtk_text_view_new());
	gtk_text_view_set_wrap_mode(fb->text, GTK_WRAP_WORD_CHAR);
	gtk_text_view_set_top_margin(fb->text, 6);
	gtk_text_view_set_left_margin(fb->text, 8);
	gtk_text_view_set_right_margin(fb->text, 8);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), GTK_WIDGET(fb->text));
	gtk_box_append(GTK_BOX(box), scroller);

	// Opt-in screenshot (OFF by default); captured only when the box is ticked.
	fb->attach_shot = GTK_CHECK_BUTTON(
		gtk_check_button_new_with_label(_("Attach screenshot of my screen")));
	g_signal_connect(fb->attach_shot, "toggled", G_CALLBACK(on_attach_shot_toggled), fb);
	gtk_box_append(GTK_BOX(box), GTK_WIDGET(fb->attach_shot));

	GtkWidget *shot_warn = gtk_label_new(
		_("A screenshot may show sensitive on-screen content. Off by default."));
	gtk_widget_add_css_class(shot_warn, "dim-label");
	gtk_widget_add_css_class(shot_warn, "caption");
	gtk_widget_set_halign(shot_warn, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(box), shot_warn);

	fb->shot_preview = gtk_image_new();
	gtk_widget_set_size_request(fb->shot_preview, -1, 120);
	gtk_widget_set_halign(fb->shot_preview, GTK_ALIGN_CENTER);
	gtk_widget_set_visible(fb->shot_preview, FALSE);
	gtk_box_append(GTK_BOX(box), fb->shot_preview);

	// Opt-in diagnostics (OFF by default).
	fb->attach_diag = GTK_CHECK_BUTTON(
		gtk_check_button_new_with_label(_("Attach diagnostics (build, OS, monitors)")));
	gtk_box_append(GTK_BOX(box), GTK_WIDGET(fb->attach_diag));

	fb->status = gtk_label_new("");
	gtk_widget_add_css_class(fb->status, "dim-label");
	gtk_label_set_wrap(GTK_LABEL(fb->status), TRUE);
	gtk_widget_set_halign(fb->status, GTK_ALIGN_START);
	gtk_widget_set_visible(fb->status, FALSE);
	gtk_box_append(GTK_BOX(box), fb->status);

	// Actions.
	GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_widget_set_halign(actions, GTK_ALIGN_END);
	gtk_widget_set_margin_top(actions, 4);
	GtkWidget *cancel = gtk_button_new_with_label(_("Cancel"));
	g_signal_connect(cancel, "clicked", G_CALLBACK(on_cancel), fb);
	fb->send_btn = gtk_button_new_with_label(_("Send"));
	gtk_widget_add_css_class(fb->send_btn, "suggested-action"); // green identity
	g_signal_connect(fb->send_btn, "clicked", G_CALLBACK(on_send), fb);
	gtk_box_append(GTK_BOX(actions), cancel);
	gtk_box_append(GTK_BOX(actions), fb->send_btn);
	gtk_box_append(GTK_BOX(box), actions);

	gtk_window_present(fb->window);
}

static void
on_bug_clicked(GtkButton *b, gpointer ud)
{
	GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(b));
	keysclicks_feedback_present(GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL,
				    (KeysClicksSettings *)ud);
}

GtkWidget *
keysclicks_feedback_button_new(KeysClicksSettings *settings)
{
	GtkWidget *btn = gtk_button_new();
	gtk_button_set_child(GTK_BUTTON(btn), bug_image_new(18));
	gtk_widget_set_tooltip_text(btn, _("Report a bug"));
	gtk_widget_add_css_class(btn, "flat");
	g_signal_connect(btn, "clicked", G_CALLBACK(on_bug_clicked), settings);
	return btn;
}
