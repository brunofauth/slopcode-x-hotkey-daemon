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

#ifndef SXHKD_INDICATOR_PROTOCOL_H
#define SXHKD_INDICATOR_PROTOCOL_H

/* The consumer side of the daemon's status FIFO protocol (version 2, see the
 * "Status FIFO" section of sxhkd(1)), as the chain indicator needs it: the
 * framing of the byte stream into lines, the parsing of one line into a
 * message, and the model that replays the messages into the chain phase and
 * progress string that indicator_derive_banner() renders. Pure C99 and libc,
 * no X: unit-tested headlessly (test/indicator_protocol_test.c).
 *
 * The protocol rule this module lives by: a prefix it does not know is
 * ignored, and every known prefix keeps its meaning and its order relative
 * to the others, so a newer daemon can add messages without breaking it. */

#include <stdbool.h>
#include <stddef.h>
#include "chain_phase.h"

/* One status line, parsed. The prefix letters are those of the protocol. */
typedef enum {
	STATUS_MESSAGE_HOTKEY,     /* H: a chord was received; the text lists the chords so far */
	STATUS_MESSAGE_BEGIN,      /* B: a chord chain has begun */
	STATUS_MESSAGE_LOCKED,     /* L: the chord chain has been locked */
	STATUS_MESSAGE_ABORTED,    /* A: the abort keysym ended the chord chain (an E follows) */
	STATUS_MESSAGE_END,        /* E: the chord chain has ended */
	STATUS_MESSAGE_TIMEOUT,    /* T: the chord chain timed out (an E follows) */
	STATUS_MESSAGE_COMMAND,    /* C: a command has been started */
	STATUS_MESSAGE_UNKNOWN,    /* a prefix this version does not know: to be ignored */
	STATUS_MESSAGE_MALFORMED   /* not a status line at all: to be ignored (see below) */
} status_message_kind_t;

/* A line is malformed when it is empty (every message has a prefix), when it
 * carries a NUL byte, or when it is an H line whose text does not fit the
 * daemon's own bound on the progress string: such a line cannot come from
 * the daemon. The text of a C line is not kept: the indicator never shows a
 * command, and a command may be as long as a configuration line. */
typedef struct {
	status_message_kind_t kind;
	union {
		struct {
			/* NUL-terminated; at most CHAIN_PROGRESS_CAPACITY - 1 bytes, as
			 * the daemon's own progress string. May be empty. */
			char text[CHAIN_PROGRESS_CAPACITY];
		} hotkey;                             /* valid iff kind == STATUS_MESSAGE_HOTKEY */
		struct {
			char prefix;
		} unknown;                            /* valid iff kind == STATUS_MESSAGE_UNKNOWN */
	} as;
} status_message_t;

/* Parses one line, given without its terminating newline, into `parsed`.
 * Always writes a complete message: an unparsable line yields
 * STATUS_MESSAGE_MALFORMED rather than nothing. */
void indicator_parse_status_line(const char *line, size_t length, status_message_t *parsed);

/* The framing of the byte stream read from the FIFO: bytes go in by chunks
 * of whatever size read() returned, complete lines come out one message
 * each, and a line cut by a chunk boundary is held until its end arrives. */

/* The longest line the buffer holds, newline excluded. Longer than any line
 * the daemon writes: a status line is at most one prefix byte plus a
 * configuration item, and the daemon writes each line atomically only up to
 * PIPE_BUF (4096 on Linux), so a longer line cannot be relied upon anyway. */
#define INDICATOR_LINE_CAPACITY 4096

typedef enum {
	LINE_BUFFER_COLLECTING,   /* the bytes held are the start of a line */
	LINE_BUFFER_DISCARDING    /* the line grew past the capacity: dropping bytes up to its newline */
} line_buffer_state_t;

typedef struct {
	line_buffer_state_t state;
	size_t length;                         /* bytes held; meaningful iff state == LINE_BUFFER_COLLECTING */
	char bytes[INDICATOR_LINE_CAPACITY];   /* the incomplete line, not NUL-terminated */
} indicator_line_buffer_t;

/* Receives each message as its line completes; `context` is the caller's. */
typedef void (*status_message_callback_t)(const status_message_t *message, void *context);

/* Empties the buffer. Also the state to return to when the writer changes
 * (the daemon restarted): a half line of the old stream must not be glued
 * to the first line of the new one. */
void indicator_line_buffer_reset(indicator_line_buffer_t *buffer);

/* Appends `byte_count` bytes and delivers, in order, every line they
 * complete. A line longer than INDICATOR_LINE_CAPACITY bytes is delivered
 * once as STATUS_MESSAGE_MALFORMED, as soon as it overflows, and the rest of
 * it up to its newline is dropped. The callback may not touch the buffer. */
void indicator_line_buffer_feed(indicator_line_buffer_t *buffer, const char *bytes, size_t byte_count, status_message_callback_t on_message, void *context);

/* The chain as the consumer sees it: the mirror of the daemon's recorder,
 * rebuilt from the messages. The pair is exactly what
 * indicator_derive_banner() takes. */
typedef struct {
	chain_phase_t phase;
	/* The chords received so far, as the last H line before or while the
	 * chain was in progress gave them; frozen once the chain is locked. Empty
	 * when idle. */
	char progress[CHAIN_PROGRESS_CAPACITY];
} chain_view_t;

/* Idle, with no progress: the state before any message and after the daemon
 * has gone. */
void indicator_view_reset(chain_view_t *view);

/* Replays one message into the view:
 *
 *   H  stores the text as the progress, except in a locked chain, where the
 *      daemon's text is the frozen prefix plus the chord just pressed and the
 *      view keeps the prefix; while idle the text is stored but not shown,
 *      since a single-chord hotkey sends H without B and must show nothing.
 *   B  the chain is in progress.
 *   L  the chain is locked.
 *   E  the chain has ended: idle, progress cleared.
 *   T, A, C, unknown and malformed lines change nothing.
 *
 * The daemon sends B, L and E in a fixed order, but the pipe may drop lines
 * when full, so each of them applies from every phase: the view converges
 * on the daemon's state at the latest on the next E. */
void indicator_view_apply(chain_view_t *view, const status_message_t *message);

#endif
