// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger
//
// keysclicks-input: the privileged half of the On-Screen Keyboard & Mouse-Click
// Visualizer — Always-On-Top Overlay for Linux/Wayland (COSMIC, sway, Hyprland,
// KDE Plasma, wlroots).
//
// It opens libinput through the udev backend on seat0, waits on the libinput
// file descriptor with poll() (fully event-driven: it blocks until the kernel
// has input for us, so it uses ~0% CPU while idle), and prints every keyboard
// key and pointer button transition to stdout as one compact JSON object per
// line. The GTK front-end launches this program through pkexec, because reading
// /dev/input requires privileges the desktop user does not normally have.
//
// Output line format (newline delimited, flushed per line):
//   {"type":"key","code":<evdev-keycode>,"pressed":<true|false>}
//   {"type":"button","code":<evdev-button-code>,"pressed":<true|false>}

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <libinput.h>
#include <libudev.h>

// libinput asks us to open/close the raw device nodes on its behalf. Running as
// root (via pkexec) we simply forward to open()/close(); no fancy seat manager
// is needed for a single-shot capture tool.
static int
open_restricted(const char *path, int flags, void *user_data)
{
	(void)user_data;
	int fd = open(path, flags);
	return fd < 0 ? -errno : fd;
}

static void
close_restricted(int fd, void *user_data)
{
	(void)user_data;
	close(fd);
}

static const struct libinput_interface interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};

static volatile sig_atomic_t keep_running = 1;

// Pointer acceleration speed to apply to every pointer device, so this context's
// relative deltas match the compositor's cursor motion. The front-end passes the
// compositor's own speed as argv[1]; without it we leave libinput's default. A
// mismatch here is exactly what makes the click-flash drift and defeats the
// (linear) calibration — see docs/behaviors.
static double g_accel_speed = 0.0;
static int g_have_accel = 0;

static void
request_stop(int signum)
{
	(void)signum;
	keep_running = 0;
}

// Kill any OTHER keysclicks-input processes left over from previous runs. We run
// as root (via pkexec), so we can reap orphaned/duplicate readers that the
// unprivileged front-end could not — otherwise they pile up holding the pointer
// devices open and add input latency. Matches /proc/<pid>/comm (truncated to 15
// chars for a 16-char name) and skips our own PID.
static void
kill_stale_backends(void)
{
	pid_t self = getpid();
	DIR *proc = opendir("/proc");
	if (proc == NULL)
		return;
	struct dirent *e;
	while ((e = readdir(proc)) != NULL) {
		pid_t pid = (pid_t)atoi(e->d_name);
		if (pid <= 0 || pid == self)
			continue;
		char path[64];
		snprintf(path, sizeof path, "/proc/%d/comm", pid);
		FILE *f = fopen(path, "r");
		if (f == NULL)
			continue;
		char comm[32] = { 0 };
		if (fgets(comm, sizeof comm, f) != NULL &&
		    strncmp(comm, "keysclicks-inpu", 15) == 0)
			kill(pid, SIGTERM);
		fclose(f);
	}
	closedir(proc);
}

static void
emit_event(const char *type, uint32_t code, int pressed)
{
	printf("{\"type\":\"%s\",\"code\":%u,\"pressed\":%s}\n", type, code,
	       pressed ? "true" : "false");
	fflush(stdout);
}

// Relative pointer motion. dx/dy are libinput's (accelerated) device deltas.
// The program never calls setlocale(), so it stays in the "C" locale and the
// numbers are always formatted with a '.' decimal separator as JSON requires.
static void
emit_motion(double dx, double dy)
{
	printf("{\"type\":\"motion\",\"dx\":%.4f,\"dy\":%.4f}\n", dx, dy);
	fflush(stdout);
}

static void
process_event(struct libinput_event *event)
{
	switch (libinput_event_get_type(event)) {
	case LIBINPUT_EVENT_KEYBOARD_KEY: {
		struct libinput_event_keyboard *kev =
			libinput_event_get_keyboard_event(event);
		uint32_t code = libinput_event_keyboard_get_key(kev);
		int pressed = libinput_event_keyboard_get_key_state(kev) ==
			      LIBINPUT_KEY_STATE_PRESSED;
		emit_event("key", code, pressed);
		break;
	}
	case LIBINPUT_EVENT_POINTER_BUTTON: {
		struct libinput_event_pointer *pev =
			libinput_event_get_pointer_event(event);
		uint32_t button = libinput_event_pointer_get_button(pev);
		int pressed = libinput_event_pointer_get_button_state(pev) ==
			      LIBINPUT_BUTTON_STATE_PRESSED;
		emit_event("button", button, pressed);
		break;
	}
	case LIBINPUT_EVENT_POINTER_MOTION: {
		struct libinput_event_pointer *pev =
			libinput_event_get_pointer_event(event);
		emit_motion(libinput_event_pointer_get_dx(pev),
			    libinput_event_pointer_get_dy(pev));
		break;
	}
	case LIBINPUT_EVENT_DEVICE_ADDED: {
		// Match the compositor's pointer acceleration so our accelerated deltas
		// track the on-screen cursor (same adaptive profile, same speed). Only
		// pointer devices expose an accel config; keyboards etc. return false.
		struct libinput_device *dev = libinput_event_get_device(event);
		if (g_have_accel &&
		    libinput_device_config_accel_is_available(dev))
			libinput_device_config_accel_set_speed(dev, g_accel_speed);
		break;
	}
	default:
		// Everything else (absolute motion, touch, device add/remove, ...)
		// is not part of what the overlay renders, so it is ignored.
		break;
	}
}

// Drain and dispatch whatever libinput has queued right now.
static int
drain_events(struct libinput *li)
{
	if (libinput_dispatch(li) != 0)
		return -1;

	struct libinput_event *event;
	while ((event = libinput_get_event(li)) != NULL) {
		process_event(event);
		libinput_event_destroy(event);
	}
	return 0;
}

int
main(int argc, char **argv)
{
	signal(SIGINT, request_stop);
	signal(SIGTERM, request_stop);

	// Reap any stale/duplicate readers from earlier runs before we start (we are
	// root here, so we can, unlike the front-end). Keeps orphans from piling up.
	kill_stale_backends();

	// Optional argv[1]: the compositor's pointer acceleration speed (-1..1). When
	// present we apply it to every pointer device so this context's deltas match
	// the cursor. strtod tolerates surrounding text; a bare number is expected.
	if (argc >= 2 && argv[1][0] != '\0') {
		g_accel_speed = strtod(argv[1], NULL);
		g_have_accel = 1;
	}

	struct udev *udev = udev_new();
	if (udev == NULL) {
		fprintf(stderr, "keysclicks-input: failed to create udev context\n");
		return 1;
	}

	struct libinput *li = libinput_udev_create_context(&interface, NULL, udev);
	if (li == NULL) {
		fprintf(stderr, "keysclicks-input: failed to create libinput context\n");
		udev_unref(udev);
		return 1;
	}

	if (libinput_udev_assign_seat(li, "seat0") != 0) {
		fprintf(stderr, "keysclicks-input: failed to assign seat0\n");
		libinput_unref(li);
		udev_unref(udev);
		return 1;
	}

	// Assigning the seat queues a burst of DEVICE_ADDED events for the
	// devices that already exist; consume them so we start from a clean slate.
	if (drain_events(li) != 0) {
		fprintf(stderr, "keysclicks-input: initial dispatch failed\n");
		libinput_unref(li);
		udev_unref(udev);
		return 1;
	}

	// Watch two fds: libinput (input to forward) and our own stdout. When the
	// front-end exits or crashes it closes the read end of the stdout pipe, which
	// makes poll() report POLLERR/POLLHUP on the write end. We run as root via
	// pkexec, so the (unprivileged) front-end cannot kill us — instead we must
	// notice the pipe closing and exit ourselves, or we leak an orphaned input
	// reader that keeps the system's pointer devices open. (A write after the
	// reader is gone would also raise SIGPIPE, but there may be no input event to
	// trigger it, so polling the pipe is what guarantees a prompt, reliable exit.)
	struct pollfd pfds[2] = {
		{ .fd = libinput_get_fd(li), .events = POLLIN },
		{ .fd = STDOUT_FILENO, .events = 0 }, // POLLERR/HUP/NVAL are always reported
	};

	while (keep_running) {
		int ready = poll(pfds, 2, -1);
		if (ready < 0) {
			if (errno == EINTR)
				continue; // interrupted by a signal, re-check the flag
			perror("keysclicks-input: poll");
			break;
		}
		// Front-end (pipe reader) went away -> stop so we do not orphan.
		if (pfds[1].revents & (POLLERR | POLLHUP | POLLNVAL))
			break;
		if ((pfds[0].revents & POLLIN) && drain_events(li) != 0) {
			fprintf(stderr, "keysclicks-input: dispatch failed\n");
			break;
		}
	}

	libinput_unref(li);
	udev_unref(udev);
	return 0;
}
