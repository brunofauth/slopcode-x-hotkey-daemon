/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (c) 2026 Bruno Fauth
 *
 * This file is part of sxhkd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/* sxhkd-indicator: the on-screen chord-chain indicator as a program of its
 * own, fed by the daemon's status FIFO (sxhkd -s PATH) instead of living
 * inside the daemon.
 *
 * Startup order: signal handlers, then the FIFO (created if absent, opened
 * blocking until the daemon opens its end), then the X connection and the
 * window. Waiting for the daemon therefore needs no display, and a Ctrl-C
 * during the wait, the likeliest moment for one, ends the program cleanly.
 * The program outlives daemon restarts: when every writer has closed the
 * pipe it hides the banner and goes back to waiting for a writer. */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include "diagnostics.h"
#include "indicator.h"
#include "indicator_options.h"
#include "indicator_protocol.h"

/* Whether this run created the FIFO, hence removes it at exit; a FIFO that
 * already existed belongs to whoever made it (the daemon, most likely, or
 * the user) and is left in place. */
typedef enum {
	STATUS_FIFO_INHERITED,
	STATUS_FIFO_CREATED
} status_fifo_ownership_t;

typedef struct {
	const char *path;
	status_fifo_ownership_t ownership;
} status_fifo_t;

/* Set by the handler of every terminating signal, read by the wait loops.
 * Initialised before the handlers are installed and never reset. */
static volatile sig_atomic_t running;

/* The FIFO this run created, for the exit-time removal. Unlike the daemon,
 * this program never forks, so an atexit() handler cannot run in a child
 * and remove the FIFO under a running parent: it is the simplest way to
 * cover every exit path, including err() out of the X and cairo setup. */
static status_fifo_t exit_time_fifo = {.path = NULL, .ownership = STATUS_FIFO_INHERITED};

static void stop_running(int signal_number)
{
	(void) signal_number;
	running = false;
}

/* signal() has System V semantics under _POSIX_C_SOURCE=200112L with glibc:
 * the disposition resets to SIG_DFL on delivery, so a second signal in quick
 * succession would kill the program. sigaction() gives the persistent
 * handler the wait loops rely on. No SA_RESTART: the blocking open() and
 * pselect() must return with EINTR so that the flag is examined promptly. */
static void install_signal_handler(int signal_number, void (*handler)(int))
{
	struct sigaction action;
	action.sa_handler = handler;
	action.sa_flags = 0;
	if (sigemptyset(&action.sa_mask) != 0 || sigaction(signal_number, &action, NULL) != 0)
		err("Can't install the handler of signal %d: %s.\n", signal_number, strerror(errno));
}

static void remove_created_status_fifo(void)
{
	switch (exit_time_fifo.ownership) {
		case STATUS_FIFO_INHERITED:
			break;
		case STATUS_FIFO_CREATED:
			unlink(exit_time_fifo.path);
			break;
	}
}

/* Creates the FIFO if nothing exists at the path, the same policy as the
 * daemon's -s: whichever of the two starts first creates it, the other one
 * inherits it, and only the creator removes it at exit. */
static status_fifo_t prepare_status_fifo(const char *fifo_path)
{
	status_fifo_t fifo = {.path = fifo_path, .ownership = STATUS_FIFO_INHERITED};
	if (mkfifo(fifo_path, S_IRUSR | S_IWUSR) == 0)
		fifo.ownership = STATUS_FIFO_CREATED;
	else if (errno != EEXIST)
		err("Can't create the status fifo '%s': %s.\n", fifo_path, strerror(errno));
	exit_time_fifo = fifo;
	if (atexit(remove_created_status_fifo) != 0)
		err("Can't register the removal of the status fifo at exit.\n");
	return fifo;
}

typedef enum {
	FIFO_OPEN_SUCCEEDED,
	FIFO_OPEN_STOPPED     /* a terminating signal arrived while waiting */
} fifo_open_kind_t;

typedef struct {
	fifo_open_kind_t kind;
	union {
		struct {
			int descriptor;
		} succeeded;   /* valid iff kind == FIFO_OPEN_SUCCEEDED */
	} as;
} fifo_open_result_t;

/* Opens the FIFO for reading, blocking until a writer (the daemon) opens
 * its end, and verifies on the descriptor that what was opened is a FIFO.
 * Must be called with the handled signals unblocked: a terminating signal
 * interrupts the wait with EINTR and the flag ends it. The check of the
 * flag and the open() are not one atomic step, so a signal delivered
 * between them is noticed only once a writer appears or the next signal
 * arrives; the persistent handler guarantees that a second one does. */
static fifo_open_result_t open_status_fifo_for_reading(const char *fifo_path)
{
	fifo_open_result_t result;
	int fifo_fd = -1;
	while (fifo_fd == -1) {
		if (!running) {
			result.kind = FIFO_OPEN_STOPPED;
			return result;
		}
		fifo_fd = open(fifo_path, O_RDONLY);
		if (fifo_fd == -1 && errno != EINTR)
			err("Can't open the status fifo '%s': %s.\n", fifo_path, strerror(errno));
	}

	struct stat fifo_status;
	if (fstat(fifo_fd, &fifo_status) != 0)
		err("Can't inspect the status fifo '%s': %s.\n", fifo_path, strerror(errno));
	if (!S_ISFIFO(fifo_status.st_mode))
		err("The status fifo path '%s' is not a fifo.\n", fifo_path);

	/* Non-blocking from now on: the main loop learns of new lines from
	 * pselect() and must never stall in read(). O_CLOEXEC and O_NONBLOCK at
	 * open time are not visible under _POSIX_C_SOURCE 200112L (and the open
	 * had to block anyway), so set both flags afterwards. */
	const int status_flags = fcntl(fifo_fd, F_GETFL);
	if (status_flags == -1 || fcntl(fifo_fd, F_SETFL, status_flags | O_NONBLOCK) == -1)
		err("Can't make the status fifo '%s' non-blocking: %s.\n", fifo_path, strerror(errno));
	if (fcntl(fifo_fd, F_SETFD, FD_CLOEXEC) == -1)
		err("Can't set close-on-exec on the status fifo '%s': %s.\n", fifo_path, strerror(errno));

	result.kind = FIFO_OPEN_SUCCEEDED;
	result.as.succeeded.descriptor = fifo_fd;
	return result;
}

/* The X connection and the screen the banner goes on: the few lines of the
 * daemon's setup() that the indicator needs, without the rest of it. */
typedef struct {
	xcb_connection_t *connection;
	xcb_screen_t *screen;
	int screen_number;
} display_t;

static display_t connect_to_display(void)
{
	display_t display;
	int screen_index = 0;
	display.connection = xcb_connect(NULL, &screen_index);
	if (xcb_connection_has_error(display.connection))
		err("Can't open display.\n");
	display.screen_number = screen_index;
	display.screen = NULL;
	xcb_screen_iterator_t screen_iterator = xcb_setup_roots_iterator(xcb_get_setup(display.connection));
	for (; screen_iterator.rem > 0; xcb_screen_next(&screen_iterator), screen_index--) {
		if (screen_index == 0) {
			display.screen = screen_iterator.data;
			break;
		}
	}
	if (display.screen == NULL)
		err("Can't acquire screen.\n");
	return display;
}

/* Everything the main loop keeps between two lines of the pipe. */
typedef struct {
	chain_view_t view;
	indicator_line_buffer_t line_buffer;
	/* When the last line was received; meaningful iff a line was received
	 * since the view was last reset, which is exactly when the view can be
	 * in progress. */
	struct timespec last_line_time;
} reader_state_t;

static struct timespec monotonic_now(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		err("Can't read the monotonic clock: %s.\n", strerror(errno));
	return now;
}

/* now + seconds, and later - now as a non-negative duration (zero when
 * `later` has passed). tv_nsec is in [0, 1e9) after both. */
static struct timespec time_after(struct timespec now, int seconds)
{
	now.tv_sec += seconds;
	return now;
}

static struct timespec time_until(struct timespec later, struct timespec now)
{
	struct timespec remaining = {0, 0};
	if (later.tv_sec < now.tv_sec || (later.tv_sec == now.tv_sec && later.tv_nsec <= now.tv_nsec))
		return remaining;
	remaining.tv_sec = later.tv_sec - now.tv_sec;
	remaining.tv_nsec = later.tv_nsec - now.tv_nsec;
	if (remaining.tv_nsec < 0) {
		remaining.tv_sec -= 1;
		remaining.tv_nsec += 1000000000L;
	}
	return remaining;
}

/* Replays one line into the view and restarts the safety timeout. */
static void on_status_message(const status_message_t *message, void *context)
{
	reader_state_t *state = context;
	indicator_view_apply(&state->view, message);
	state->last_line_time = monotonic_now();
}

typedef enum {
	READ_MORE_LATER,     /* the pipe is drained for now */
	READ_WRITER_GONE     /* end-of-file: every writer closed the pipe */
} read_outcome_t;

/* Drains what the pipe holds into the line buffer. */
static read_outcome_t read_status_lines(int fifo_fd, reader_state_t *state)
{
	char chunk[INDICATOR_LINE_CAPACITY];
	for (;;) {
		const ssize_t byte_count = read(fifo_fd, chunk, sizeof(chunk));
		if (byte_count > 0) {
			indicator_line_buffer_feed(&state->line_buffer, chunk, (size_t) byte_count, on_status_message, state);
			continue;
		}
		if (byte_count == 0)
			return READ_WRITER_GONE;
		if (errno == EAGAIN)
			return READ_MORE_LATER;
		if (errno == EINTR)
			continue;
		err("Can't read the status fifo: %s.\n", strerror(errno));
	}
}

/* The response type of an event, without the bit that marks it as sent by
 * another client (SendEvent). xcb/xcb_event.h of xcb-util has the same
 * macro; this program has no other use for that library. */
static uint8_t event_response_type(const xcb_generic_event_t *event)
{
	return event->response_type & 0x7f;
}

static void dispatch_x_event(xcb_generic_event_t *event)
{
	switch (event_response_type(event)) {
		case XCB_EXPOSE:
			indicator_handle_expose((const xcb_expose_event_t *) event);
			break;
		case XCB_CONFIGURE_NOTIFY:
			indicator_handle_configure_notify((const xcb_configure_notify_event_t *) event);
			break;
		default:
			/* Errors (type 0) and whatever else the server sends: nothing
			 * to do, the banner window selects Expose only. */
			break;
	}
}

static void drain_x_events(xcb_connection_t *connection)
{
	xcb_generic_event_t *event = xcb_poll_for_event(connection);
	while (event != NULL) {
		dispatch_x_event(event);
		free(event);
		event = xcb_poll_for_event(connection);
	}
}

typedef enum {
	SESSION_ENDED_BY_SIGNAL,
	SESSION_ENDED_BY_WRITER_EXIT,
	SESSION_ENDED_BY_DISPLAY_ERROR
} session_end_t;

/* Serves one writer: from an opened pipe until it reaches end-of-file, a
 * terminating signal or the loss of the X connection. Runs with the handled
 * signals blocked; `original_signal_mask` is what pselect() waits under. */
static session_end_t run_session(int fifo_fd, const display_t *display, int timeout_in_seconds, const sigset_t *original_signal_mask)
{
	static reader_state_t state;
	indicator_view_reset(&state.view);
	indicator_line_buffer_reset(&state.line_buffer);
	const int x_fd = xcb_get_file_descriptor(display->connection);
	const int highest_fd = (x_fd > fifo_fd) ? x_fd : fifo_fd;

	while (running) {
		/* A chain in progress that the daemon ended with a line the pipe
		 * dropped would stay on screen forever; the safety timeout hides it,
		 * like the daemon's own timeout would have. A locked chain has no
		 * timeout, in the daemon either. */
		const struct timespec *wait_limit = NULL;
		struct timespec remaining_time;
		switch (state.view.phase) {
			case CHAIN_PHASE_IDLE:
			case CHAIN_PHASE_LOCKED:
				break;
			case CHAIN_PHASE_IN_PROGRESS:
				if (timeout_in_seconds > 0) {
					remaining_time = time_until(time_after(state.last_line_time, timeout_in_seconds), monotonic_now());
					wait_limit = &remaining_time;
				}
				break;
		}

		/* Events that libxcb read while waiting for a reply (cairo-xcb does
		 * that) sit in its queue without making the descriptor readable, so
		 * look there before blocking in pselect(). */
		xcb_generic_event_t *queued_event = xcb_poll_for_queued_event(display->connection);
		bool fifo_readable = false;
		if (queued_event != NULL) {
			dispatch_x_event(queued_event);
			free(queued_event);
		} else {
			fd_set readable_descriptors;
			FD_ZERO(&readable_descriptors);
			FD_SET(x_fd, &readable_descriptors);
			FD_SET(fifo_fd, &readable_descriptors);
			const int ready_count = pselect(highest_fd + 1, &readable_descriptors, NULL, NULL, wait_limit, original_signal_mask);
			if (ready_count == -1 && errno != EINTR)
				err("Can't wait for the status fifo and the display: %s.\n", strerror(errno));
			if (ready_count > 0)
				fifo_readable = FD_ISSET(fifo_fd, &readable_descriptors);
			if (ready_count == 0) {
				/* The wait limit is only ever set for a chain in progress. */
				indicator_view_reset(&state.view);
			}
		}
		drain_x_events(display->connection);

		read_outcome_t read_outcome = READ_MORE_LATER;
		if (fifo_readable)
			read_outcome = read_status_lines(fifo_fd, &state);
		switch (read_outcome) {
			case READ_MORE_LATER:
				break;
			case READ_WRITER_GONE:
				indicator_view_reset(&state.view);
				break;
		}

		/* Every path that changes the view (lines, timeout, end-of-file) has
		 * run by now; make the screen match. The indicator flushes whatever
		 * it draws itself. */
		indicator_sync_with_chain_phase(state.view.phase, state.view.progress);

		if (xcb_connection_has_error(display->connection))
			return SESSION_ENDED_BY_DISPLAY_ERROR;
		switch (read_outcome) {
			case READ_MORE_LATER:
				break;
			case READ_WRITER_GONE:
				return SESSION_ENDED_BY_WRITER_EXIT;
		}
	}
	return SESSION_ENDED_BY_SIGNAL;
}

int main(int argc, char *argv[])
{
	const indicator_command_line_t command_line = parse_indicator_command_line(argc, argv);
	switch (command_line.kind) {
		case INDICATOR_COMMAND_LINE_SHOW_HELP:
			print_indicator_usage(stdout);
			return EXIT_SUCCESS;
		case INDICATOR_COMMAND_LINE_SHOW_VERSION:
			printf("%s\n", VERSION);
			return EXIT_SUCCESS;
		case INDICATOR_COMMAND_LINE_INVALID:
			err("%s\nTry 'sxhkd-indicator --help' for more information.\n", command_line.as.invalid.message);
		case INDICATOR_COMMAND_LINE_RUN:
			break;
	}
	const indicator_run_options_t *options = &command_line.as.run;

	running = true;
	install_signal_handler(SIGINT, stop_running);
	install_signal_handler(SIGHUP, stop_running);
	install_signal_handler(SIGTERM, stop_running);

	const status_fifo_t fifo = prepare_status_fifo(options->status_fifo_path);
	fifo_open_result_t opened = open_status_fifo_for_reading(fifo.path);
	switch (opened.kind) {
		case FIFO_OPEN_STOPPED:
			return EXIT_SUCCESS;
		case FIFO_OPEN_SUCCEEDED:
			break;
	}
	int fifo_fd = opened.as.succeeded.descriptor;

	const display_t display = connect_to_display();
	indicator_settings_t settings;
	settings.kind = INDICATOR_SETTINGS_ENABLED;
	settings.as.enabled = options->config;
	indicator_init(&settings, display.connection, display.screen, display.screen_number);

	/* The handled signals stay blocked while the loop body runs and are only
	 * delivered inside pselect(), which installs the original mask atomically
	 * for the duration of the wait, and around the blocking reopen of the
	 * pipe, which must be interruptible. */
	sigset_t handled_signals;
	sigset_t original_signal_mask;
	sigemptyset(&handled_signals);
	sigaddset(&handled_signals, SIGINT);
	sigaddset(&handled_signals, SIGHUP);
	sigaddset(&handled_signals, SIGTERM);
	if (sigprocmask(SIG_BLOCK, &handled_signals, &original_signal_mask) != 0)
		err("Can't block the handled signals.\n");

	int exit_status = EXIT_SUCCESS;
	bool serving = true;
	while (serving) {
		const session_end_t session_end = run_session(fifo_fd, &display, options->timeout_in_seconds, &original_signal_mask);
		switch (session_end) {
			case SESSION_ENDED_BY_SIGNAL:
				serving = false;
				break;
			case SESSION_ENDED_BY_DISPLAY_ERROR:
				warn("The server closed the connection.\n");
				exit_status = EXIT_FAILURE;
				serving = false;
				break;
			case SESSION_ENDED_BY_WRITER_EXIT:
				/* The daemon exited; the banner is hidden already. Wait for
				 * the next one with the signals deliverable, then block them
				 * again for the loop. */
				close(fifo_fd);
				fifo_fd = -1;
				if (sigprocmask(SIG_SETMASK, &original_signal_mask, NULL) != 0)
					err("Can't unblock the handled signals.\n");
				opened = open_status_fifo_for_reading(fifo.path);
				if (sigprocmask(SIG_BLOCK, &handled_signals, NULL) != 0)
					err("Can't block the handled signals.\n");
				switch (opened.kind) {
					case FIFO_OPEN_STOPPED:
						serving = false;
						break;
					case FIFO_OPEN_SUCCEEDED:
						fifo_fd = opened.as.succeeded.descriptor;
						break;
				}
				break;
		}
	}

	indicator_shutdown();
	xcb_disconnect(display.connection);
	if (fifo_fd != -1)
		close(fifo_fd);
	/* The created FIFO, if any, goes with the atexit() handler. */
	return exit_status;
}
