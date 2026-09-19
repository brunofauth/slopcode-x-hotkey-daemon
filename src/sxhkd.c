/* Copyright (c) 2013, Bastien Dejean
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Modifications Copyright (c) 2026 Bruno Fauth
 *
 * This file is part of a fork of sxhkd distributed as a whole under the GNU
 * General Public License, version 3 or (at your option) any later version;
 * see LICENSE. The original code remains available under the BSD 2-Clause
 * license reproduced above and in LICENSE.BSD-2-Clause.
 */

#include <xcb/xcb_event.h>
#include <xcb/xkb.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include "parse.h"
#include "grab.h"
#include "indicator.h"
#include "options.h"

xcb_connection_t *dpy;
xcb_screen_t *screen;
int screen_number;
xcb_window_t root;
xcb_key_symbols_t *symbols;

char *shell;
char config_file[MAXLEN];
char **extra_confs;
int num_extra_confs;
int redir_fd;
status_fifo_t status_fifo;
char progress[3 * MAXLEN];
int mapping_count;
int timeout;

char sxhkd_pid[MAXLEN];

hotkey_t *hotkeys_head, *hotkeys_tail;
bool grabbed;
volatile sig_atomic_t running, toggle_grab, reload, bell;
sigset_t original_signal_mask;
chain_phase_t chain_phase;
xcb_keysym_t abort_keysym;
chord_t *abort_chord;

uint16_t num_lock;
uint16_t caps_lock;
uint16_t scroll_lock;

int main(int argc, char *argv[])
{
	status_fifo.kind = STATUS_FIFO_ABSENT;
	grabbed = false;
	redir_fd = -1;
	abort_keysym = ESCAPE_KEYSYM;

	const command_line_t command_line = parse_command_line(argc, argv);
	switch (command_line.kind) {
		case COMMAND_LINE_SHOW_HELP:
			print_usage(stdout);
			return EXIT_SUCCESS;
		case COMMAND_LINE_SHOW_VERSION:
			printf("%s\n", VERSION);
			return EXIT_SUCCESS;
		case COMMAND_LINE_INVALID:
			err("%s\nTry 'sxhkd --help' for more information.\n", command_line.as.invalid.message);
		case COMMAND_LINE_RUN:
			break;
	}
	const run_options_t *options = &command_line.as.run;

	mapping_count = options->mapping_count;
	timeout = options->timeout_in_seconds;
	num_extra_confs = options->extra_config_count;
	extra_confs = options->extra_config_paths;

	if (options->redirect_path != NULL) {
		redir_fd = open(options->redirect_path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		if (redir_fd == -1)
			err("Can't open the command redirection file '%s': %s.\n", options->redirect_path, strerror(errno));
	}

	if (options->abort_keysym_name != NULL) {
		/* parse_keysym only reads its argument; its signature predates const. */
		if (!parse_keysym((char *) options->abort_keysym_name, &abort_keysym))
			err("Invalid keysym name: '%s'.\n", options->abort_keysym_name);
	}

	if (options->indicator_look_given_without_position)
		warn("The indicator font and colors have no effect without --indicator.\n");

	if (options->config_path == NULL) {
		char *config_home = getenv(CONFIG_HOME_ENV);
		if (config_home != NULL)
			snprintf(config_file, sizeof(config_file), "%s/%s", config_home, CONFIG_PATH);
		else
			snprintf(config_file, sizeof(config_file), "%s/%s/%s", getenv("HOME"), ".config", CONFIG_PATH);
	} else {
		snprintf(config_file, sizeof(config_file), "%s", options->config_path);
	}

	if (options->status_fifo_path != NULL)
		status_fifo = open_status_fifo(options->status_fifo_path);

	signal(SIGINT, hold);
	signal(SIGHUP, hold);
	signal(SIGTERM, hold);
	signal(SIGUSR1, hold);
	signal(SIGUSR2, hold);
	signal(SIGALRM, hold);

	setup();
	indicator_init(&options->indicator, dpy, screen, screen_number);
	get_standard_keysyms();
	get_lock_fields();
	abort_chord = make_chord(abort_keysym, XCB_NONE, 0, XCB_KEY_PRESS, false, false);
	load_config(config_file);
	for (int i = 0; i < num_extra_confs; i++)
		load_config(extra_confs[i]);
	grab();

	xcb_generic_event_t *evt;
	int fd = xcb_get_file_descriptor(dpy);

	fd_set descriptors;

	reload = toggle_grab = bell = false;
	chain_phase = CHAIN_PHASE_IDLE;
	running = true;

	/* The handled signals stay blocked while the loop body runs and are only
	 * delivered inside pselect(), which installs the original mask atomically
	 * for the duration of the wait. A signal can therefore no longer slip in
	 * between the flag checks and the wait, where it would have gone
	 * unnoticed until the next X event. Children restore the original mask
	 * before exec (see execute()). */
	sigset_t handled_signals;
	sigemptyset(&handled_signals);
	sigaddset(&handled_signals, SIGINT);
	sigaddset(&handled_signals, SIGHUP);
	sigaddset(&handled_signals, SIGTERM);
	sigaddset(&handled_signals, SIGUSR1);
	sigaddset(&handled_signals, SIGUSR2);
	sigaddset(&handled_signals, SIGALRM);
	if (sigprocmask(SIG_BLOCK, &handled_signals, &original_signal_mask) != 0)
		err("Can't block the handled signals.\n");

	xcb_flush(dpy);

	while (running) {
		/* Events that libxcb read while waiting for a reply (cairo-xcb does
		 * that) sit in its queue without making the descriptor readable, so
		 * look there before blocking in pselect(). */
		evt = xcb_poll_for_queued_event(dpy);
		if (evt == NULL) {
			FD_ZERO(&descriptors);
			FD_SET(fd, &descriptors);
			if (pselect(fd + 1, &descriptors, NULL, NULL, NULL, &original_signal_mask) > 0)
				evt = xcb_poll_for_event(dpy);
		}
		while (evt != NULL) {
			uint8_t event_type = XCB_EVENT_RESPONSE_TYPE(evt);
			switch (event_type) {
				case XCB_KEY_PRESS:
				case XCB_KEY_RELEASE:
				case XCB_BUTTON_PRESS:
				case XCB_BUTTON_RELEASE:
					key_button_event(evt, event_type);
					break;
				case XCB_MAPPING_NOTIFY:
					mapping_notify(evt);
					break;
				case XCB_EXPOSE:
					indicator_handle_expose((const xcb_expose_event_t *) evt);
					break;
				case XCB_CONFIGURE_NOTIFY:
					indicator_handle_configure_notify((const xcb_configure_notify_event_t *) evt);
					break;
				default:
					PRINTF("received event %u\n", event_type);
					break;
			}
			free(evt);
			evt = xcb_poll_for_event(dpy);
		}

		if (reload) {
			signal(SIGUSR1, hold);
			reload_cmd();
			reload = false;
		}

		if (toggle_grab) {
			signal(SIGUSR2, hold);
			toggle_grab_cmd();
			toggle_grab = false;
		}

		if (bell) {
			signal(SIGALRM, hold);
			put_status(TIMEOUT_PREFIX, "Timeout reached");
			abort_chain();
			bell = false;
		}

		/* Every path that changes the recorder state (key events, timeout,
		 * reload, mapping notify) has run by now; make the screen match. */
		indicator_sync_with_chain_phase(chain_phase, progress);

		if (xcb_connection_has_error(dpy)) {
			warn("The server closed the connection.\n");
			running = false;
		}
	}

	if (redir_fd != -1) {
		close(redir_fd);
	}

	close_status_fifo();

	ungrab();
	indicator_shutdown();
	cleanup();
	destroy_chord(abort_chord);
	xcb_key_symbols_free(symbols);
	xcb_disconnect(dpy);
	return EXIT_SUCCESS;
}

void key_button_event(xcb_generic_event_t *evt, uint8_t event_type)
{
	xcb_keysym_t keysym = XCB_NO_SYMBOL;
	xcb_button_t button = XCB_NONE;
	bool replay_event = false;
	uint16_t modfield = 0;
	uint16_t lockfield = num_lock | caps_lock | scroll_lock;
	parse_event(evt, event_type, &keysym, &button, &modfield);
	modfield &= ~lockfield & MOD_STATE_FIELD;
	if (keysym != XCB_NO_SYMBOL || button != XCB_NONE) {
		hotkey_t *hk = find_hotkey(keysym, button, modfield, event_type, &replay_event);
		if (hk != NULL) {
			/* A synchronous command blocks this process until it finishes; let
			 * the screen reflect the chain state before that rather than after. */
			if (hk->sync)
				indicator_sync_with_chain_phase(chain_phase, progress);
			run(hk->command, hk->sync);
			put_status(COMMAND_PREFIX, hk->command);
		}
	}
	switch (event_type) {
		case XCB_BUTTON_PRESS:
		case XCB_BUTTON_RELEASE:
			if (replay_event)
				xcb_allow_events(dpy, XCB_ALLOW_REPLAY_POINTER, XCB_CURRENT_TIME);
			else
				xcb_allow_events(dpy, XCB_ALLOW_SYNC_POINTER, XCB_CURRENT_TIME);
			break;
		case XCB_KEY_PRESS:
		case XCB_KEY_RELEASE:
			if (replay_event)
				xcb_allow_events(dpy, XCB_ALLOW_REPLAY_KEYBOARD, XCB_CURRENT_TIME);
			else
				xcb_allow_events(dpy, XCB_ALLOW_SYNC_KEYBOARD, XCB_CURRENT_TIME);
			break;
	}
	xcb_flush(dpy);
}

void mapping_notify(xcb_generic_event_t *evt)
{
	if (!mapping_count)
		return;
	xcb_mapping_notify_event_t *e = (xcb_mapping_notify_event_t *) evt;
	PRINTF("mapping notify %u %u\n", e->request, e->count);
	if (e->request == XCB_MAPPING_POINTER)
		return;
	if (xcb_refresh_keyboard_mapping(symbols, e) == 1) {
		destroy_chord(abort_chord);
		get_lock_fields();
		reload_cmd();
		abort_chord = make_chord(abort_keysym, XCB_NONE, 0, XCB_KEY_PRESS, false, false);
		if (mapping_count > 0)
			mapping_count--;
	}
}

void setup(void)
{
	int screen_idx;
	dpy = xcb_connect(NULL, &screen_idx);
	if (xcb_connection_has_error(dpy))
		err("Can't open display.\n");
	screen_number = screen_idx;
	xcb_xkb_use_extension(dpy, XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION);
	xcb_xkb_per_client_flags(dpy, XCB_XKB_ID_USE_CORE_KBD, XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 1, 0, 0, 0);
	screen = NULL;
	xcb_screen_iterator_t screen_iter = xcb_setup_roots_iterator(xcb_get_setup(dpy));
	for (; screen_iter.rem; xcb_screen_next(&screen_iter), screen_idx--) {
		if (screen_idx == 0) {
			screen = screen_iter.data;
			break;
		}
	}
	if (screen == NULL)
		err("Can't acquire screen.\n");
	root = screen->root;
	if ((shell = getenv(SXHKD_SHELL_ENV)) == NULL && (shell = getenv(SHELL_ENV)) == NULL)
		err("The '%s' environment variable is not defined.\n", SHELL_ENV);
	symbols = xcb_key_symbols_alloc(dpy);
	hotkeys_head = hotkeys_tail = NULL;
	progress[0] = '\0';

	snprintf(sxhkd_pid, MAXLEN, "%i", getpid());
	setenv("SXHKD_PID", sxhkd_pid, 1);
}

void cleanup(void)
{
	PUTS("cleanup");
	hotkey_t *hk = hotkeys_head;
	while (hk != NULL) {
		hotkey_t *next = hk->next;
		destroy_chain(hk->chain);
		free(hk->cycle);
		free(hk);
		hk = next;
	}
	hotkeys_head = hotkeys_tail = NULL;
}

void reload_cmd(void)
{
	PUTS("reload");
	/* The chains about to be destroyed may be mid-recording; end the
	 * recording first so that nothing of it survives into the new bindings.
	 * The grabs are rebuilt from scratch below anyway. */
	if (chain_phase != CHAIN_PHASE_IDLE)
		reset_chain_recorder();
	cleanup();
	load_config(config_file);
	for (int i = 0; i < num_extra_confs; i++)
		load_config(extra_confs[i]);
	ungrab();
	grab();
}

void toggle_grab_cmd(void)
{
	PUTS("toggle grab");
	if (grabbed) {
		/* No further chord can arrive once the bindings are released, so a
		 * chain in progress would otherwise linger until the next grab. */
		if (chain_phase != CHAIN_PHASE_IDLE)
			reset_chain_recorder();
		ungrab();
	} else {
		grab();
	}
}

void hold(int sig)
{
	if (sig == SIGHUP || sig == SIGINT || sig == SIGTERM)
		running = false;
	else if (sig == SIGUSR1)
		reload = true;
	else if (sig == SIGUSR2)
		toggle_grab = true;
	else if (sig == SIGALRM)
		bell = true;
}

/* Aborts startup because the status FIFO is unusable, removing it first if
 * this run created it. There is deliberately no atexit() handler for the
 * removal: spawn()'s children exit through exit()/err() too and must never
 * remove the FIFO under the running daemon. As a consequence a fatal error
 * later in startup leaves a created FIFO behind, which the next run reuses. */
static void fail_status_fifo(const char *fifo_path, status_fifo_ownership_t ownership, const char *problem)
{
	const int saved_errno = errno;
	switch (ownership) {
		case STATUS_FIFO_INHERITED:
			break;
		case STATUS_FIFO_CREATED:
			unlink(fifo_path);
			break;
	}
	err("%s the status fifo '%s': %s.\n", problem, fifo_path, strerror(saved_errno));
}

/* Creates the FIFO if nothing exists at the path, opens it and verifies that
 * what was opened really is a FIFO (on the descriptor, so that the check
 * cannot be raced). A pre-existing FIFO is used as-is and left in place. */
status_fifo_t open_status_fifo(const char *fifo_path)
{
	status_fifo_ownership_t ownership = STATUS_FIFO_INHERITED;
	if (mkfifo(fifo_path, S_IRUSR | S_IWUSR) == 0)
		ownership = STATUS_FIFO_CREATED;
	else if (errno != EEXIST)
		fail_status_fifo(fifo_path, ownership, "Can't create");

	const int fifo_fd = open(fifo_path, O_RDWR | O_NONBLOCK);
	if (fifo_fd == -1)
		fail_status_fifo(fifo_path, ownership, "Can't open");

	/* Commands must not inherit the FIFO: a child holding a write end keeps
	 * readers from seeing end-of-file after sxhkd exits, and could read or
	 * inject status lines. O_CLOEXEC is not visible under _POSIX_C_SOURCE
	 * 200112L, so set the flag afterwards. */
	if (fcntl(fifo_fd, F_SETFD, FD_CLOEXEC) == -1)
		fail_status_fifo(fifo_path, ownership, "Can't set close-on-exec on");

	struct stat fifo_status;
	if (fstat(fifo_fd, &fifo_status) != 0)
		fail_status_fifo(fifo_path, ownership, "Can't inspect");
	if (!S_ISFIFO(fifo_status.st_mode)) {
		errno = 0;
		switch (ownership) {
			case STATUS_FIFO_INHERITED:
				break;
			case STATUS_FIFO_CREATED:
				unlink(fifo_path);
				break;
		}
		err("The status fifo path '%s' is not a fifo.\n", fifo_path);
	}

	FILE *stream = fdopen(fifo_fd, "w");
	if (stream == NULL)
		fail_status_fifo(fifo_path, ownership, "Can't open");

	status_fifo_t opened;
	opened.kind = STATUS_FIFO_PRESENT;
	opened.as.present.stream = stream;
	opened.as.present.ownership = ownership;
	opened.as.present.path = fifo_path;
	return opened;
}

void close_status_fifo(void)
{
	switch (status_fifo.kind) {
		case STATUS_FIFO_ABSENT:
			return;
		case STATUS_FIFO_PRESENT:
			break;
	}
	fclose(status_fifo.as.present.stream);
	switch (status_fifo.as.present.ownership) {
		case STATUS_FIFO_INHERITED:
			break;
		case STATUS_FIFO_CREATED:
			unlink(status_fifo.as.present.path);
			break;
	}
	status_fifo.kind = STATUS_FIFO_ABSENT;
}

void put_status(char c, const char *s)
{
	switch (status_fifo.kind) {
		case STATUS_FIFO_ABSENT:
			return;
		case STATUS_FIFO_PRESENT:
			break;
	}
	fprintf(status_fifo.as.present.stream, "%c%s\n", c, s);
	fflush(status_fifo.as.present.stream);
}
