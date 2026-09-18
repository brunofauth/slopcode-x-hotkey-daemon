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

#define CONFIG_HOME_ENV     "XDG_CONFIG_HOME"
#define SXHKD_SHELL_ENV     "SXHKD_SHELL"
#define SHELL_ENV           "SHELL"
#define CONFIG_PATH         "sxhkd/sxhkdrc"
#define HOTKEY_PREFIX       'H'
#define COMMAND_PREFIX      'C'
#define BEGIN_CHAIN_PREFIX  'B'
#define END_CHAIN_PREFIX    'E'
#define TIMEOUT_PREFIX      'T'

extern xcb_connection_t *dpy;
extern xcb_screen_t *screen;
extern int screen_number;
extern xcb_window_t root;
extern xcb_key_symbols_t *symbols;

extern char *shell;
extern char config_file[MAXLEN];
extern char **extra_confs;
extern int num_extra_confs;
extern int redir_fd;
typedef enum {
	STATUS_FIFO_ABSENT,    /* -s not given */
	STATUS_FIFO_PRESENT
} status_fifo_kind_t;

typedef enum {
	STATUS_FIFO_INHERITED, /* the FIFO existed already: left in place at exit */
	STATUS_FIFO_CREATED    /* created by sxhkd: removed at exit */
} status_fifo_ownership_t;

typedef struct {
	status_fifo_kind_t kind;
	union {
		struct {
			FILE *stream;
			status_fifo_ownership_t ownership;
			const char *path;   /* points into argv[]: process lifetime */
		} present;
	} as;
} status_fifo_t;

extern status_fifo_t status_fifo;
extern char progress[3 * MAXLEN];
extern int mapping_count;
extern int timeout;

extern hotkey_t *hotkeys_head, *hotkeys_tail;
extern bool grabbed;
/* Written from the signal handler, read by the main loop. */
extern volatile sig_atomic_t running, toggle_grab, reload, bell;
/* The signal mask sxhkd was started with; the main loop blocks the handled
 * signals outside pselect(), and children must not inherit that. */
extern sigset_t original_signal_mask;
extern chain_phase_t chain_phase;
extern xcb_keysym_t abort_keysym;
extern chord_t *abort_chord;

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
void close_status_fifo(void);
void put_status(char c, const char *s);

#endif
