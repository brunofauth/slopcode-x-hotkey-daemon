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

#ifndef SXHKD_SXHKD_H
#define SXHKD_SXHKD_H

#include <xcb/xcb_keysyms.h>
#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include "types.h"
#include "helpers.h"
#include "options.h"

#define CONFIG_HOME_ENV     "XDG_CONFIG_HOME"
#define SXHKD_SHELL_ENV     "SXHKD_SHELL"
#define SHELL_ENV           "SHELL"
#define CONFIG_PATH         "sxhkd/sxhkdrc"
/* Status FIFO protocol, version 2: one line per message, a one-character
 * prefix followed by a text (see the "Status FIFO" section of the man page).
 * Prefixes are never reused with another meaning and their relative order
 * never changes; a consumer must ignore the prefixes it does not know, so that
 * later versions can add some. */
#define HOTKEY_PREFIX        'H' /* the chords received so far */
#define COMMAND_PREFIX       'C' /* a command was started: its text */
#define BEGIN_CHAIN_PREFIX   'B' /* "Begin chain": after the H of a chain's first chord */
#define END_CHAIN_PREFIX     'E' /* "End chain": the chain ended, whatever the cause */
#define TIMEOUT_PREFIX       'T' /* "Timeout reached": before the E of a timed-out chain */
#define LOCKED_CHAIN_PREFIX  'L' /* "Chain locked": the chain entered the locked phase (version 2) */
#define ABORTED_CHAIN_PREFIX 'A' /* "Chain aborted": before the E of a chain ended by the abort keysym (version 2) */

extern xcb_connection_t *dpy;
extern xcb_window_t root;
extern xcb_key_symbols_t *symbols;

extern char *shell;
extern char config_file[MAXLEN];
extern char **extra_confs;
extern int num_extra_confs;
extern int redir_fd;
typedef enum {
	STATUS_FIFO_INHERITED, /* the FIFO existed already: left in place at exit */
	STATUS_FIFO_CREATED    /* created by sxhkd: removed at exit */
} status_fifo_ownership_t;

/* One open status FIFO. */
typedef struct {
	FILE *stream;
	status_fifo_ownership_t ownership;
	const char *path;   /* points into argv[]: process lifetime */
} status_fifo_t;

/* The status FIFOs given with -s, in order: every message goes to each of
 * them. Exactly the first status_fifo_count elements are open; 0: -s was
 * not given. */
extern status_fifo_t status_fifos[MAX_STATUS_FIFOS];
extern int status_fifo_count;
extern char progress[CHAIN_PROGRESS_CAPACITY];
extern int mapping_count;
extern int timeout;

extern hotkey_t *hotkeys_head, *hotkeys_tail;
extern bool grabbed;
/* Written from the signal handler, read by the main loop. */
extern volatile sig_atomic_t running, toggle_grab, reload, bell;
/* The signal mask sxhkd was started with; the main loop blocks the handled
 * signals outside pselect() and while it waits for a synchronous command
 * (see spawn()), and children must not inherit that. Valid only once the
 * main loop's mask setup has run; run() is only ever called from the loop. */
extern sigset_t original_signal_mask;
extern chain_phase_t chain_phase;
extern xcb_keysym_t abort_keysym;
/* Rebuilt on every handled mapping notify (see mapping_notify()). */
extern abort_chord_t abort_chord;

extern uint16_t num_lock;
extern uint16_t caps_lock;
extern uint16_t scroll_lock;

void key_button_event(xcb_generic_event_t *evt, uint8_t event_type);
void mapping_notify(xcb_generic_event_t *evt);
void setup(void);
void cleanup(void);
void reload_cmd(void);
void toggle_grab_cmd(void);
void hold(int sig);
status_fifo_t open_status_fifo(const char *fifo_path);
void close_status_fifos(void);
void put_status(char prefix, const char *text);

#endif
