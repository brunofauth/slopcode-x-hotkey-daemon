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
