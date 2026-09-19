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

#ifndef SXHKD_TYPES_H
#define SXHKD_TYPES_H

#include <xcb/xcb_keysyms.h>
#include <stdbool.h>
#include "helpers.h"
#include "chain_phase.h"

#define KEYSYMS_PER_KEYCODE  4
#define MOD_STATE_FIELD      255
#define ESCAPE_KEYSYM        0xff1b
#define SYNCHRONOUS_CHAR     ';'

typedef struct chord_t chord_t;
struct chord_t {
	char repr[MAXLEN];
	xcb_keysym_t keysym;
	xcb_button_t button;
	uint16_t modfield;
	uint8_t event_type;
	bool replay_event;
	bool lock_chain;
	chord_t *next;
	chord_t *more;
};

typedef struct {
	chord_t *head;
	chord_t *tail;
	chord_t *state;
} chain_t;

typedef struct {
	int period;
	int delay;
} cycle_t;

typedef struct hotkey_t hotkey_t;
struct hotkey_t {
	chain_t *chain;
	char command[2 * MAXLEN];
	bool sync;
	cycle_t *cycle;
	hotkey_t *next;
	hotkey_t *prev;
};

typedef struct {
	char *name;
	xcb_keysym_t keysym;
} keysym_dict_t;

/* The chord that aborts a chord chain, built from the abort keysym against
 * the current keymap. A keysym with no keycode in that keymap (a valid name
 * given to -a but absent from the layout, or a layout switch under -m that
 * dropped it) yields no chord at all; that state is spelled out here rather
 * than carried as a null pointer, so that every user has to decide what an
 * unavailable abort chord means for it. */
typedef enum {
	/* The abort keysym has a keycode: the chord is grabbed while a chain is
	 * in progress and pressing it aborts the chain. */
	ABORT_CHORD_AVAILABLE,
	/* The abort keysym has no keycode: a chain can end only by reaching a
	 * tail, by timeout, or (a locked chain) by a reload or a grab toggle. */
	ABORT_CHORD_UNAVAILABLE
} abort_chord_kind_t;

typedef struct {
	abort_chord_kind_t kind;
	union {
		chord_t *chord; /* valid iff kind == ABORT_CHORD_AVAILABLE; never NULL */
	} as;
} abort_chord_t;

hotkey_t *find_hotkey(xcb_keysym_t keysym, xcb_button_t button, uint16_t modfield, uint8_t event_type, bool *replay_event);
bool match_chord(chord_t *chord, uint8_t event_type, xcb_keysym_t keysym, xcb_button_t button, uint16_t modfield);
bool chains_interfere(chain_t* a, chain_t* b);
/* Returns NULL for a key chord whose keysym has no keycode in the current
 * keymap (the parser relies on this to reject the hotkey); never for a
 * button chord. */
chord_t *make_chord(xcb_keysym_t keysym, xcb_button_t button, uint16_t modfield, uint8_t event_type, bool replay_event, bool lock_chain);
/* Wraps make_chord() for the abort keysym: ABORT_CHORD_UNAVAILABLE when the
 * keysym has no keycode in the current keymap. */
abort_chord_t make_abort_chord(xcb_keysym_t keysym_that_aborts_chains);
/* Frees the chord, if there is one. The argument is not usable afterwards. */
void destroy_abort_chord(abort_chord_t destroyed_abort_chord);
void add_chord(chain_t *chain, chord_t *chord);
chain_t *make_chain(void);
cycle_t *make_cycle(int delay, int period);
hotkey_t *make_hotkey(chain_t *chain, char *command);
void add_hotkey(hotkey_t *hk);
/* Ends the chord chain in progress without touching the grabs: emits the end
 * status, rewinds every chain to its head, goes idle and cancels the timeout. */
void reset_chain_recorder(void);
/* reset_chain_recorder() followed by restoring the grabs of the chain heads. */
void abort_chain(void);
void destroy_chain(chain_t *chain);
/* The chord must not be NULL. */
void destroy_chord(chord_t *chord);

#endif
