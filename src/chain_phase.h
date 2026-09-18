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

/* The character placed between chord texts when the chain progress string is
 * joined. It is the ';' link separator of the configuration syntax, which can
 * therefore never occur inside a single chord's text. */
#define CHAIN_PROGRESS_SEPARATOR ';'

/* The phase of the chord-chain recorder. Exactly one phase holds at any time.
 *
 * This replaces the former pair of booleans `chained` and `locked`, under which
 * the combination "locked but not chained" was representable yet meaningless
 * (and reachable through a cycle hotkey whose tail chord carried a lock mark).
 *
 * This header is deliberately free of X and libc includes so that modules
 * which must stay headless-testable can include it. */
typedef enum {
	/* No chain is in progress: every chain sits at its head chord. */
	CHAIN_PHASE_IDLE,
	/* At least one chain advanced past its head. The chain aborts when a
	 * tail is reached, when the abort keysym is pressed, or on timeout. */
	CHAIN_PHASE_IN_PROGRESS,
	/* A chord followed by ':' matched: the chain stays active at its tail
	 * until the abort keysym is pressed (no timeout applies). */
	CHAIN_PHASE_LOCKED
} chain_phase_t;

#endif
