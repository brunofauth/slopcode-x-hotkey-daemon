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

#ifndef SXHKD_CHAIN_PHASE_H
#define SXHKD_CHAIN_PHASE_H

/* What the chord-chain recorder and the chain indicator share: the phase
 * enumeration and the shape of the chain progress string.
 *
 * This header is deliberately free of X and libc includes so that modules
 * which must stay headless-testable, and the indicator binary, can include
 * it. */

/* The capacity of the fixed-size text buffers of the configuration parser: a
 * chord text, a keysym name, a token. Larger items fit in a small multiple of
 * it. Defined here rather than in helpers.h because CHAIN_PROGRESS_CAPACITY
 * below derives from it. */
#define MAXLEN                   256

/* The character placed between chord texts when the chain progress string is
 * joined. It is the ';' link separator of the configuration syntax, which can
 * therefore never occur inside a single chord's text. */
#define CHAIN_PROGRESS_SEPARATOR ';'

/* The size, NUL included, of the buffer holding the chain progress string:
 * the chord texts received so far, each at most MAXLEN - 1 bytes, joined by
 * CHAIN_PROGRESS_SEPARATOR. The recorder appends with truncation, so a
 * longer chain is cut off rather than overflowing. */
#define CHAIN_PROGRESS_CAPACITY  (3 * MAXLEN)

/* The phase of the chord-chain recorder. Exactly one phase holds at any time.
 *
 * This replaces the former pair of booleans `chained` and `locked`, under which
 * the combination "locked but not chained" was representable yet meaningless
 * (and reachable through a cycle hotkey whose tail chord carried a lock mark). */
typedef enum {
	/* No chain is in progress: every chain sits at its head chord, no alarm
	 * is armed and the abort chord is not grabbed. A hotkey that fully
	 * matches in the same event as chains that merely advance wins over
	 * them, and those chains are rewound before the event is over, so that
	 * the phase can stay idle. */
	CHAIN_PHASE_IDLE,
	/* At least one chain sits past its head: waiting for its next chord, or
	 * (a cycle hotkey) resting at its tail so that the tail chord can be
	 * pressed again. A timeout applies: whenever `timeout` is positive an
	 * alarm is armed, and every chord received re-arms it. The chain ends
	 * when a non-cycle tail is reached, when the abort keysym is pressed,
	 * when no chain is left past its head, on timeout, on reload and on a
	 * grab toggle. */
	CHAIN_PHASE_IN_PROGRESS,
	/* A chord followed by ':' matched: the chains that reached their tail
	 * stay there and their tail chords keep firing, with no timeout (no
	 * alarm is ever armed in this phase), until the abort keysym is pressed
	 * or the configuration is reloaded or the bindings are ungrabbed.
	 * Chains that are past their head but not at their tail are dormant. */
	CHAIN_PHASE_LOCKED
} chain_phase_t;

#endif
