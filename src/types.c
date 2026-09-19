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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include "parse.h"
#include "grab.h"

/* Whether a chain takes no part in matching the current event, given the
 * phase the recorder was in when the event arrived. */
static bool chain_is_dormant(const chain_t *chain, chain_phase_t chain_phase_at_entry)
{
	switch (chain_phase_at_entry) {
		case CHAIN_PHASE_IDLE:
			return false;
		case CHAIN_PHASE_IN_PROGRESS:
			return chain->state == chain->head;
		case CHAIN_PHASE_LOCKED:
			return chain->state == chain->head || chain->state != chain->tail;
	}
	return false;
}

/* Emits the hotkey status line for a chord that just matched and records the
 * chain progress. Only chords that advance a chain are persisted: the tail
 * chord of a locked chain is reported as part of the full hotkey text, while
 * the progress string keeps describing the locked prefix, i.e. the mode. */
static void report_matched_chord(const chord_t *matched_chord, chain_phase_t chain_phase_at_entry)
{
	const char separator[2] = {CHAIN_PROGRESS_SEPARATOR, '\0'};
	switch (chain_phase_at_entry) {
		case CHAIN_PHASE_IDLE:
			snprintf(progress, sizeof(progress), "%s", matched_chord->repr);
			put_status(HOTKEY_PREFIX, progress);
			break;
		case CHAIN_PHASE_IN_PROGRESS:
			strncat(progress, separator, sizeof(progress) - strlen(progress) - 1);
			strncat(progress, matched_chord->repr, sizeof(progress) - strlen(progress) - 1);
			put_status(HOTKEY_PREFIX, progress);
			break;
		case CHAIN_PHASE_LOCKED: {
			/* Large enough for progress, the separator and a repr: never truncates. */
			char hotkey_text[sizeof(progress) + sizeof(separator) + MAXLEN];
			snprintf(hotkey_text, sizeof(hotkey_text), "%s%s%s", progress, separator, matched_chord->repr);
			put_status(HOTKEY_PREFIX, hotkey_text);
			break;
		}
	}
}

static void grab_abort_chord(void)
{
	switch (abort_chord.kind) {
		case ABORT_CHORD_AVAILABLE:
			grab_chord(abort_chord.as.chord);
			break;
		case ABORT_CHORD_UNAVAILABLE:
			break;
	}
}

static bool abort_chord_matches(uint8_t event_type, xcb_keysym_t keysym, xcb_button_t button, uint16_t modfield)
{
	switch (abort_chord.kind) {
		case ABORT_CHORD_AVAILABLE:
			return match_chord(abort_chord.as.chord, event_type, keysym, button, modfield);
		case ABORT_CHORD_UNAVAILABLE:
			return false;
	}
	return false;
}

/* Undoes what the current event did to the chains while the recorder was
 * idle, because a hotkey fully matched in that same event and wins over the
 * chains that merely advanced: rewinds every chain to its head and restores
 * the grabs of the chain heads, dropping the grabs issued for the chords
 * expected next. Nothing is reported and no timeout is touched: no chain was
 * announced with a begin-chain status, and an idle recorder has no alarm. */
static void discard_partial_matches(void)
{
	PUTS("discard partial matches");
	for (hotkey_t *hk = hotkeys_head; hk != NULL; hk = hk->next)
		hk->chain->state = hk->chain->head;
	ungrab();
	grab();
}

/* What the walk over the hotkeys concluded about the event. */
typedef enum {
	/* No hotkey's command runs; the event may have advanced or rewound
	 * chains. */
	EVENT_OUTCOME_NO_HOTKEY,
	/* A cycle hotkey's command runs. Its chain stays at its tail so that
	 * repeated presses of the tail chord keep cycling; when that chain has
	 * more than one chord it therefore stays advanced, and the recorder must
	 * stay out of the idle phase (with its timeout running) for it. */
	EVENT_OUTCOME_CYCLE_HOTKEY,
	/* A hotkey's command runs and its chain is finished with. */
	EVENT_OUTCOME_COMPLETE_HOTKEY
} event_outcome_kind_t;

typedef struct {
	event_outcome_kind_t kind;
	union {
		hotkey_t *hotkey; /* valid iff kind != EVENT_OUTCOME_NO_HOTKEY; never NULL */
	} as;
} event_outcome_t;

/* Phase transitions of the chain recorder, by phase at entry and outcome of
 * the walk (see the table in chain_phase.h):
 *
 *   IDLE, no hotkey, no chain advanced         -> IDLE
 *   IDLE, no hotkey, chains advanced           -> IN_PROGRESS, or LOCKED when
 *                                                 a ':' chord matched (B, abort
 *                                                 chord grabbed, alarm armed)
 *   IDLE, a hotkey fired                       -> IDLE; the chains that only
 *                                                 advanced are rewound
 *   IN_PROGRESS, no hotkey, none active        -> IDLE via abort_chain (E),
 *                                                 then the event is matched
 *                                                 again from IDLE
 *   IN_PROGRESS, no hotkey, abort chord        -> same
 *   IN_PROGRESS, no hotkey, chains active      -> IN_PROGRESS (alarm re-armed),
 *                                                 or LOCKED when a ':' chord
 *                                                 matched
 *   IN_PROGRESS, cycle hotkey fired            -> same as the line above
 *   IN_PROGRESS, complete hotkey fired         -> IDLE via abort_chain (E)
 *   LOCKED, no hotkey, none active             -> IDLE via abort_chain (E),
 *                                                 then matched again from IDLE
 *   LOCKED, no hotkey, abort chord             -> same
 *   LOCKED, otherwise                          -> LOCKED (no alarm)
 *
 * A hotkey that fully matches wins over the abort chord, as it did upstream:
 * the abort chord is only consulted when no hotkey fired.
 *
 * Only the first hotkey whose chain reaches its tail fires, except that the
 * hotkeys of a cycle group all sit at the same tail and each keeps its own
 * turn counter; the walk therefore goes on past a cycle hotkey and stops at
 * a non-cycle one, so that a later non-cycle hotkey with the same chords
 * wins over the cycle group (upstream behaviour). */
hotkey_t *find_hotkey(xcb_keysym_t keysym, xcb_button_t button, uint16_t modfield, uint8_t event_type, bool *replay_event)
{
	/* The loop below never changes the phase: everything inside it reasons
	 * about the phase as it was when the event arrived, and the transitions
	 * happen after it. */
	const chain_phase_t chain_phase_at_entry = chain_phase;
	/* The chains advanced past their head once this event is processed
	 * (a cycle hotkey's chain resting at a tail that is not its head counts). */
	int num_active = 0;
	int num_locked = 0;
	bool hotkey_reported = false;
	event_outcome_t outcome;
	outcome.kind = EVENT_OUTCOME_NO_HOTKEY;

	for (hotkey_t *hk = hotkeys_head; hk != NULL; hk = hk->next) {
		chain_t *c = hk->chain;
		if (chain_is_dormant(c, chain_phase_at_entry))
			continue;
		if (match_chord(c->state, event_type, keysym, button, modfield)) {
			/* Report the first chain that matches, whatever came before it. */
			if (!hotkey_reported) {
				hotkey_reported = true;
				report_matched_chord(c->state, chain_phase_at_entry);
			}
			if (replay_event != NULL && c->state->replay_event)
				*replay_event = true;
			if (c->state->lock_chain) {
				num_locked += 1;
				if (timeout > 0)
					alarm(0);
			}
			if (c->state == c->tail) {
				if (hk->cycle != NULL) {
					unsigned char delay = hk->cycle->delay;
					hk->cycle->delay = (delay == 0 ? hk->cycle->period - 1 : delay - 1);
					if (delay == 0) {
						outcome.kind = EVENT_OUTCOME_CYCLE_HOTKEY;
						outcome.as.hotkey = hk;
					}
					if (c->state != c->head)
						num_active++;
					continue;
				}
				outcome.kind = EVENT_OUTCOME_COMPLETE_HOTKEY;
				outcome.as.hotkey = hk;
				break;
			} else {
				c->state = c->state->next;
				num_active++;
				grab_chord(c->state);
			}
		} else {
			switch (chain_phase_at_entry) {
				case CHAIN_PHASE_IDLE:
					break;
				case CHAIN_PHASE_IN_PROGRESS:
					if (c->state->event_type == event_type)
						c->state = c->head;
					else
						num_active++;
					break;
				case CHAIN_PHASE_LOCKED:
					num_active++;
					break;
			}
		}
	}

	switch (chain_phase_at_entry) {
		case CHAIN_PHASE_IDLE:
			switch (outcome.kind) {
				case EVENT_OUTCOME_NO_HOTKEY:
					if (num_active > 0) {
						chain_phase = (num_locked > 0) ? CHAIN_PHASE_LOCKED : CHAIN_PHASE_IN_PROGRESS;
						put_status(BEGIN_CHAIN_PREFIX, "Begin chain");
						grab_abort_chord();
					}
					break;
				case EVENT_OUTCOME_CYCLE_HOTKEY:
				case EVENT_OUTCOME_COMPLETE_HOTKEY:
					/* From idle, a chain reaches its tail only if that tail is
					 * also its head, so the hotkey that fired has a single chord
					 * and any active chain is another hotkey whose chain begins
					 * with that same chord (a configuration the parser warns
					 * about). The complete hotkey wins and the others give up:
					 * this is what already happened when the single-chord
					 * hotkey came first in the configuration (the walk stopped
					 * at it before the longer chains were examined), so the two
					 * orders now agree. Not rewinding would leave those chains
					 * advanced while idle, where their next chord would fire
					 * them on its own, again and again. */
					if (num_active > 0)
						discard_partial_matches();
					break;
			}
			break;
		case CHAIN_PHASE_IN_PROGRESS:
			switch (outcome.kind) {
				case EVENT_OUTCOME_NO_HOTKEY:
					if (num_locked > 0)
						chain_phase = CHAIN_PHASE_LOCKED;
					if (num_active == 0 || abort_chord_matches(event_type, keysym, button, modfield)) {
						abort_chain();
						return find_hotkey(keysym, button, modfield, event_type, replay_event);
					}
					break;
				case EVENT_OUTCOME_CYCLE_HOTKEY:
					/* The cycle's chain is still advanced, so it was counted
					 * active above: the chain goes on, and locks if its tail
					 * chord asked for it. */
					if (num_locked > 0)
						chain_phase = CHAIN_PHASE_LOCKED;
					break;
				case EVENT_OUTCOME_COMPLETE_HOTKEY:
					abort_chain();
					break;
			}
			break;
		case CHAIN_PHASE_LOCKED:
			switch (outcome.kind) {
				case EVENT_OUTCOME_NO_HOTKEY:
					if (num_active == 0 || abort_chord_matches(event_type, keysym, button, modfield)) {
						abort_chain();
						return find_hotkey(keysym, button, modfield, event_type, replay_event);
					}
					break;
				case EVENT_OUTCOME_CYCLE_HOTKEY:
				case EVENT_OUTCOME_COMPLETE_HOTKEY:
					/* The chain stays locked at its tail: the hotkey can fire
					 * again until the lock is released. */
					break;
			}
			break;
	}

	/* The timeout follows the phase the recorder is now in: only a chain in
	 * progress has one, and every chord received re-arms it. */
	switch (chain_phase) {
		case CHAIN_PHASE_IDLE:
		case CHAIN_PHASE_LOCKED:
			break;
		case CHAIN_PHASE_IN_PROGRESS:
			if (timeout > 0)
				alarm(timeout);
			break;
	}
	PRINTF("num active %i\n", num_active);

	switch (outcome.kind) {
		case EVENT_OUTCOME_NO_HOTKEY:
			return NULL;
		case EVENT_OUTCOME_CYCLE_HOTKEY:
		case EVENT_OUTCOME_COMPLETE_HOTKEY:
			return outcome.as.hotkey;
	}
	return NULL;
}

bool match_chord(chord_t *chord, uint8_t event_type, xcb_keysym_t keysym, xcb_button_t button, uint16_t modfield)
{
	for (chord_t *c = chord; c != NULL; c = c->more)
		if (c->event_type == event_type && c->keysym == keysym && c->button == button && (c->modfield == XCB_MOD_MASK_ANY || c->modfield == modfield))
			return true;
	return false;
}

bool chains_interfere(chain_t* a, chain_t* b) {
	chord_t* i = a->head;
	chord_t* j = b->head;
	for (; i && j; i = i->next, j = j->next) {
		bool match = false;
		for (chord_t* k = i; k; k = k->more) {
			if (match_chord(j, k->event_type, k->keysym, k->button, k->modfield)) {
				match = true;
				break;
			}
		}
		if (!match) {
			break;
		}
	}
	bool result = !i || !j;
	return result;
}

chord_t *make_chord(xcb_keysym_t keysym, xcb_button_t button, uint16_t modfield, uint8_t event_type, bool replay_event, bool lock_chain)
{
	chord_t *chord;
	if (button == XCB_NONE) {
		chord_t *prev = NULL;
		chord_t *orig = NULL;
		xcb_keycode_t *keycodes = keycodes_from_keysym(keysym);
		if (keycodes != NULL) {
			for (xcb_keycode_t *kc = keycodes; *kc != XCB_NO_SYMBOL; kc++) {
				xcb_keysym_t natural_keysym = xcb_key_symbols_get_keysym(symbols, *kc, 0);
				for (unsigned char col = 0; col < KEYSYMS_PER_KEYCODE; col++) {
					xcb_keysym_t ks = xcb_key_symbols_get_keysym(symbols, *kc, col);
					if (ks == keysym) {
						uint16_t implicit_modfield = (col & 1 ? XCB_MOD_MASK_SHIFT : 0) | (col & 2 ? XCB_MOD_MASK_5 : 0);
						uint16_t explicit_modfield = modfield | implicit_modfield;
						chord = malloc(sizeof(chord_t));
						bool unique = true;
						for (chord_t *c = orig; unique && c != NULL; c = c->more)
							if (c->modfield == explicit_modfield && c->keysym == natural_keysym)
								unique = false;
						if (!unique) {
							free(chord);
							break;
						}
						chord->keysym = natural_keysym;
						chord->button = button;
						chord->modfield = explicit_modfield;
						chord->next = chord->more = NULL;
						chord->event_type = event_type;
						chord->replay_event = replay_event;
						chord->lock_chain = lock_chain;
						if (prev != NULL)
							prev->more = chord;
						else
							orig = chord;
						prev = chord;
						PRINTF("key chord %u %u\n", natural_keysym, explicit_modfield);
						break;
					}
				}
			}
		} else {
			warn("No keycodes found for keysym %u.\n", keysym);
		}
		free(keycodes);
		chord = orig;
	} else {
		chord = malloc(sizeof(chord_t));
		chord->keysym = keysym;
		chord->button = button;
		chord->modfield = modfield;
		chord->event_type = event_type;
		chord->replay_event = replay_event;
		chord->lock_chain = lock_chain;
		chord->next = chord->more = NULL;
		PRINTF("button chord %u %u\n", button, modfield);
	}
	return chord;
}

abort_chord_t make_abort_chord(xcb_keysym_t keysym_that_aborts_chains)
{
	abort_chord_t made;
	chord_t *chord = make_chord(keysym_that_aborts_chains, XCB_NONE, 0, XCB_KEY_PRESS, false, false);
	if (chord == NULL) {
		made.kind = ABORT_CHORD_UNAVAILABLE;
	} else {
		made.kind = ABORT_CHORD_AVAILABLE;
		made.as.chord = chord;
	}
	return made;
}

void destroy_abort_chord(abort_chord_t destroyed_abort_chord)
{
	switch (destroyed_abort_chord.kind) {
		case ABORT_CHORD_AVAILABLE:
			destroy_chord(destroyed_abort_chord.as.chord);
			break;
		case ABORT_CHORD_UNAVAILABLE:
			break;
	}
}

void add_chord(chain_t *chain, chord_t *chord)
{
	if (chain->head == NULL) {
		chain->head = chain->tail = chain->state = chord;
	} else {
		chain->tail->next = chord;
		chain->tail = chord;
	}
}

chain_t *make_chain(void)
{
	chain_t *chain = malloc(sizeof(chain_t));
	chain->head = chain->tail = chain->state = NULL;
	return chain;
}

cycle_t *make_cycle(int delay, int period)
{
	cycle_t *cycle = malloc(sizeof(cycle_t));
	cycle->delay = delay;
	cycle->period = period;
	return cycle;
}

hotkey_t *make_hotkey(chain_t *chain, char *command)
{
	hotkey_t *hk = malloc(sizeof(hotkey_t));
	hk->chain = chain;
	hk->sync = false;
	if (command[0] == SYNCHRONOUS_CHAR) {
		command = lgraph(command+1);
		hk->sync = true;
	}
	snprintf(hk->command, sizeof(hk->command), "%s", command);
	hk->cycle = NULL;
	hk->next = hk->prev = NULL;
	return hk;
}

void add_hotkey(hotkey_t *hk)
{
	if (hotkeys_head == NULL) {
		hotkeys_head = hotkeys_tail = hk;
	} else {
		hotkeys_tail->next = hk;
		hk->prev = hotkeys_tail;
		hotkeys_tail = hk;
	}
}

void reset_chain_recorder(void)
{
	PUTS("reset chain recorder");
	put_status(END_CHAIN_PREFIX, "End chain");
	for (hotkey_t *hk = hotkeys_head; hk != NULL; hk = hk->next)
		hk->chain->state = hk->chain->head;
	chain_phase = CHAIN_PHASE_IDLE;
	if (timeout > 0)
		alarm(0);
}

void abort_chain(void)
{
	PUTS("abort chain");
	reset_chain_recorder();
	/* Drop the grabs of the chords that were expected next (and of the abort
	 * keysym) and restore the grabs of every chain head. */
	ungrab();
	grab();
}

void destroy_chain(chain_t *chain)
{
	chord_t *c = chain->head;
	while (c != NULL) {
		chord_t *n = c->next;
		destroy_chord(c);
		c = n;
	}
	free(chain);
}

void destroy_chord(chord_t *chord)
{
	chord_t *c = chord->more;
	while (c != NULL) {
		chord_t *n = c->more;
		free(c);
		c = n;
	}
	free(chord);
}
