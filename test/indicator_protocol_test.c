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

/* Headless unit test for src/indicator_protocol.c: the line parser, the
 * framing of the byte stream, and the chain view replayed from the message
 * sequences of the "Status FIFO" section of sxhkd(1). The banner derived
 * from the view is compared with what the in-process indicator shows for
 * the same chain (see test/indicator_core_test.c). Built and run by
 * `make check`; needs no X server, cairo or pango. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "indicator_core.h"
#include "indicator_protocol.h"

static unsigned int failed_check_count = 0;
static unsigned int passed_check_count = 0;

#define CHECK(condition) \
	do { \
		if (condition) { \
			passed_check_count++; \
		} else { \
			failed_check_count++; \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

/* Parses a string literal as one line, without a newline. */
static status_message_t parse(const char *line)
{
	static status_message_t message;
	memset(&message, 0x55, sizeof(message));
	indicator_parse_status_line(line, strlen(line), &message);
	return message;
}

static void test_parse_every_prefix(void)
{
	status_message_t message = parse("Hsuper + m");
	CHECK(message.kind == STATUS_MESSAGE_HOTKEY);
	CHECK(strcmp(message.as.hotkey.text, "super + m") == 0);

	message = parse("Hsuper + m;h");
	CHECK(message.kind == STATUS_MESSAGE_HOTKEY);
	CHECK(strcmp(message.as.hotkey.text, "super + m;h") == 0);

	CHECK(parse("BBegin chain").kind == STATUS_MESSAGE_BEGIN);
	CHECK(parse("LChain locked").kind == STATUS_MESSAGE_LOCKED);
	CHECK(parse("AChain aborted").kind == STATUS_MESSAGE_ABORTED);
	CHECK(parse("EEnd chain").kind == STATUS_MESSAGE_END);
	CHECK(parse("TTimeout reached").kind == STATUS_MESSAGE_TIMEOUT);
	CHECK(parse("Cecho H").kind == STATUS_MESSAGE_COMMAND);

	/* The text of the fixed messages is not checked: only the prefix carries meaning. */
	CHECK(parse("B").kind == STATUS_MESSAGE_BEGIN);
	CHECK(parse("Ewhatever").kind == STATUS_MESSAGE_END);
	CHECK(parse("C").kind == STATUS_MESSAGE_COMMAND);
}

static void test_parse_hotkey_text_bounds(void)
{
	/* An empty H text is a valid (if odd) message, as an empty chord text is. */
	status_message_t message = parse("H");
	CHECK(message.kind == STATUS_MESSAGE_HOTKEY);
	CHECK(message.as.hotkey.text[0] == '\0');

	/* The longest text the daemon can produce, then one byte more. */
	static char line[CHAIN_PROGRESS_CAPACITY + 2];
	line[0] = 'H';
	memset(line + 1, 'a', CHAIN_PROGRESS_CAPACITY - 1);
	line[CHAIN_PROGRESS_CAPACITY] = '\0';
	message = parse(line);
	CHECK(message.kind == STATUS_MESSAGE_HOTKEY);
	CHECK(strlen(message.as.hotkey.text) == CHAIN_PROGRESS_CAPACITY - 1);

	line[CHAIN_PROGRESS_CAPACITY] = 'a';
	line[CHAIN_PROGRESS_CAPACITY + 1] = '\0';
	CHECK(parse(line).kind == STATUS_MESSAGE_MALFORMED);
}

static void test_parse_unknown_and_malformed(void)
{
	status_message_t message = parse("Xsomething new");
	CHECK(message.kind == STATUS_MESSAGE_UNKNOWN);
	CHECK(message.as.unknown.prefix == 'X');
	message = parse("h");   /* prefixes are case-sensitive */
	CHECK(message.kind == STATUS_MESSAGE_UNKNOWN);
	CHECK(message.as.unknown.prefix == 'h');
	CHECK(parse(" ").kind == STATUS_MESSAGE_UNKNOWN);

	CHECK(parse("").kind == STATUS_MESSAGE_MALFORMED);

	/* A NUL byte inside the line: the length says 5, the string says 1. */
	static status_message_t nul_message;
	indicator_parse_status_line("H\0abc", 5, &nul_message);
	CHECK(nul_message.kind == STATUS_MESSAGE_MALFORMED);
	indicator_parse_status_line("B\0abc", 5, &nul_message);
	CHECK(nul_message.kind == STATUS_MESSAGE_MALFORMED);
}

/* Collects the messages a buffer delivers, in order. */
#define COLLECTED_CAPACITY 16

typedef struct {
	status_message_t messages[COLLECTED_CAPACITY];
	size_t count;
} collected_messages_t;

static void collect_message(const status_message_t *message, void *context)
{
	collected_messages_t *collected = context;
	if (collected->count < COLLECTED_CAPACITY) {
		collected->messages[collected->count] = *message;
		collected->count++;
	}
}

/* Feeds a string literal (without its NUL) to the buffer. */
static void feed(indicator_line_buffer_t *buffer, collected_messages_t *collected, const char *bytes)
{
	indicator_line_buffer_feed(buffer, bytes, strlen(bytes), collect_message, collected);
}

static bool collected_hotkey_is(const collected_messages_t *collected, size_t index, const char *expected_text)
{
	if (index >= collected->count || collected->messages[index].kind != STATUS_MESSAGE_HOTKEY)
		return false;
	return strcmp(collected->messages[index].as.hotkey.text, expected_text) == 0;
}

static void test_line_buffer_chunk_boundaries(void)
{
	static indicator_line_buffer_t buffer;
	static collected_messages_t collected;
	indicator_line_buffer_reset(&buffer);

	/* One line in two chunks: nothing until the newline arrives. */
	collected.count = 0;
	feed(&buffer, &collected, "Hsuper");
	CHECK(collected.count == 0);
	feed(&buffer, &collected, " + m\n");
	CHECK(collected.count == 1);
	CHECK(collected_hotkey_is(&collected, 0, "super + m"));

	/* Two lines in one chunk, in order. */
	collected.count = 0;
	feed(&buffer, &collected, "BBegin chain\nHsuper + m;h\n");
	CHECK(collected.count == 2);
	CHECK(collected.messages[0].kind == STATUS_MESSAGE_BEGIN);
	CHECK(collected_hotkey_is(&collected, 1, "super + m;h"));

	/* A chunk ending exactly at the newline, then a chunk starting a new line. */
	collected.count = 0;
	feed(&buffer, &collected, "EEnd chain\n");
	CHECK(collected.count == 1);
	CHECK(collected.messages[0].kind == STATUS_MESSAGE_END);
	feed(&buffer, &collected, "Cecho H");
	CHECK(collected.count == 1);
	feed(&buffer, &collected, "\n");
	CHECK(collected.count == 2);
	CHECK(collected.messages[1].kind == STATUS_MESSAGE_COMMAND);

	/* One byte at a time. */
	collected.count = 0;
	const char *line = "Ha;b\n";
	for (const char *cursor = line; *cursor != '\0'; cursor++)
		indicator_line_buffer_feed(&buffer, cursor, 1, collect_message, &collected);
	CHECK(collected.count == 1);
	CHECK(collected_hotkey_is(&collected, 0, "a;b"));

	/* A reset drops a half line: the next writer starts afresh. */
	collected.count = 0;
	feed(&buffer, &collected, "Hsuper + ");
	indicator_line_buffer_reset(&buffer);
	feed(&buffer, &collected, "Hsuper + n\n");
	CHECK(collected.count == 1);
	CHECK(collected_hotkey_is(&collected, 0, "super + n"));

	/* An empty line is delivered, as malformed. */
	collected.count = 0;
	feed(&buffer, &collected, "\n");
	CHECK(collected.count == 1);
	CHECK(collected.messages[0].kind == STATUS_MESSAGE_MALFORMED);
}

static void test_line_buffer_overlong_line(void)
{
	static indicator_line_buffer_t buffer;
	static collected_messages_t collected;
	static char overlong[2 * INDICATOR_LINE_CAPACITY];
	indicator_line_buffer_reset(&buffer);

	/* Exactly the capacity, newline excluded: the longest line that is kept. */
	collected.count = 0;
	overlong[0] = 'C';
	memset(overlong + 1, 'x', INDICATOR_LINE_CAPACITY - 1);
	overlong[INDICATOR_LINE_CAPACITY] = '\n';
	overlong[INDICATOR_LINE_CAPACITY + 1] = '\0';
	feed(&buffer, &collected, overlong);
	CHECK(collected.count == 1);
	CHECK(collected.messages[0].kind == STATUS_MESSAGE_COMMAND);

	/* One byte more: malformed once, the rest dropped, then a normal line. */
	collected.count = 0;
	overlong[INDICATOR_LINE_CAPACITY] = 'x';
	overlong[INDICATOR_LINE_CAPACITY + 1] = 'x';
	overlong[INDICATOR_LINE_CAPACITY + 2] = '\0';
	feed(&buffer, &collected, overlong);
	CHECK(collected.count == 1);
	CHECK(collected.messages[0].kind == STATUS_MESSAGE_MALFORMED);
	feed(&buffer, &collected, "xxxxxxxx");   /* still the same line */
	CHECK(collected.count == 1);
	feed(&buffer, &collected, "xxx\nHsuper + m\n");
	CHECK(collected.count == 2);
	CHECK(collected_hotkey_is(&collected, 1, "super + m"));

	/* An over-long line that spans many chunks is still reported once. */
	collected.count = 0;
	for (size_t fed = 0; fed < 3 * INDICATOR_LINE_CAPACITY; fed += 100)
		indicator_line_buffer_feed(&buffer, overlong + 1, 100, collect_message, &collected);
	CHECK(collected.count == 1);
	CHECK(collected.messages[0].kind == STATUS_MESSAGE_MALFORMED);
	feed(&buffer, &collected, "\nBBegin chain\n");
	CHECK(collected.count == 2);
	CHECK(collected.messages[1].kind == STATUS_MESSAGE_BEGIN);
}

/* Applies each line of a NULL-terminated array to the view, in order. */
static void replay(chain_view_t *view, const char *const *lines)
{
	for (const char *const *line = lines; *line != NULL; line++) {
		const status_message_t message = parse(*line);
		indicator_view_apply(view, &message);
	}
}

static bool view_is(const chain_view_t *view, chain_phase_t phase, const char *progress)
{
	return view->phase == phase && strcmp(view->progress, progress) == 0;
}

/* What the on-screen banner would read for the view; "" when absent. */
static const char *banner_text_of(const chain_view_t *view)
{
	static indicator_banner_t banner;
	indicator_derive_banner(view->phase, view->progress, &banner);
	switch (banner.kind) {
		case INDICATOR_BANNER_ABSENT:
			return "";
		case INDICATOR_BANNER_PRESENT:
			return banner.text;
	}
	return "";
}

static bool banner_is_absent(const chain_view_t *view)
{
	static indicator_banner_t banner;
	indicator_derive_banner(view->phase, view->progress, &banner);
	return banner.kind == INDICATOR_BANNER_ABSENT;
}

static void test_view_plain_chain(void)
{
	chain_view_t view;
	indicator_view_reset(&view);
	CHECK(view_is(&view, CHAIN_PHASE_IDLE, ""));
	CHECK(banner_is_absent(&view));

	/* The man page's `super + m ; h` sequence, one step at a time. */
	replay(&view, (const char *const[]) {"Hsuper + m", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IDLE, "super + m"));
	CHECK(banner_is_absent(&view));   /* not until B: this may be a single-chord hotkey */
	replay(&view, (const char *const[]) {"BBegin chain", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IN_PROGRESS, "super + m"));
	CHECK(strcmp(banner_text_of(&view), "super + m ;") == 0);
	replay(&view, (const char *const[]) {"Hsuper + m;h", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IN_PROGRESS, "super + m;h"));
	CHECK(strcmp(banner_text_of(&view), "super + m ; h ;") == 0);
	replay(&view, (const char *const[]) {"EEnd chain", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IDLE, ""));
	CHECK(banner_is_absent(&view));
	replay(&view, (const char *const[]) {"Cecho H", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IDLE, ""));
	CHECK(banner_is_absent(&view));
}

static void test_view_single_chord_hotkey(void)
{
	chain_view_t view;
	indicator_view_reset(&view);
	replay(&view, (const char *const[]) {"Hsuper + a", "Cxterm", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IDLE, "super + a"));
	CHECK(banner_is_absent(&view));

	/* And a chain right after it starts from that chain's own chords. */
	replay(&view, (const char *const[]) {"Hsuper + m", "BBegin chain", NULL});
	CHECK(strcmp(banner_text_of(&view), "super + m ;") == 0);
}

static void test_view_locked_on_first_chord(void)
{
	chain_view_t view;
	indicator_view_reset(&view);

	/* The man page's `super + m : h` sequence. */
	replay(&view, (const char *const[]) {"Hsuper + m", "BBegin chain", "LChain locked", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_LOCKED, "super + m"));
	CHECK(strcmp(banner_text_of(&view), "super + m :") == 0);

	/* Each tail press: the H carries the frozen prefix plus the chord; the view keeps the prefix. */
	replay(&view, (const char *const[]) {"Hsuper + m;h", "Cecho H", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_LOCKED, "super + m"));
	CHECK(strcmp(banner_text_of(&view), "super + m :") == 0);
	replay(&view, (const char *const[]) {"Hsuper + m;h", "Cecho H", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_LOCKED, "super + m"));

	/* Escape. */
	replay(&view, (const char *const[]) {"AChain aborted", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_LOCKED, "super + m"));   /* A alone changes nothing */
	replay(&view, (const char *const[]) {"EEnd chain", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IDLE, ""));
	CHECK(banner_is_absent(&view));
}

static void test_view_locked_later(void)
{
	chain_view_t view;
	indicator_view_reset(&view);

	/* `super + n ; h : j`: the lock arrives after the second chord. */
	replay(&view, (const char *const[]) {"Hsuper + n", "BBegin chain", "Hsuper + n;h", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IN_PROGRESS, "super + n;h"));
	CHECK(strcmp(banner_text_of(&view), "super + n ; h ;") == 0);
	replay(&view, (const char *const[]) {"LChain locked", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_LOCKED, "super + n;h"));
	CHECK(strcmp(banner_text_of(&view), "super + n ; h :") == 0);
	replay(&view, (const char *const[]) {"Hsuper + n;h;j", "Cecho J", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_LOCKED, "super + n;h"));
	CHECK(strcmp(banner_text_of(&view), "super + n ; h :") == 0);
	replay(&view, (const char *const[]) {"AChain aborted", "EEnd chain", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IDLE, ""));
}

static void test_view_timeout_and_abort(void)
{
	chain_view_t view;
	indicator_view_reset(&view);

	replay(&view, (const char *const[]) {"Hsuper + m", "BBegin chain", "TTimeout reached", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IN_PROGRESS, "super + m"));   /* T alone changes nothing */
	replay(&view, (const char *const[]) {"EEnd chain", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IDLE, ""));

	replay(&view, (const char *const[]) {"Hsuper + m", "BBegin chain", "AChain aborted", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IN_PROGRESS, "super + m"));
	replay(&view, (const char *const[]) {"EEnd chain", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IDLE, ""));
	CHECK(banner_is_absent(&view));
}

static void test_view_reload_ends_locked_chain(void)
{
	chain_view_t view;
	indicator_view_reset(&view);

	/* A reload sends E only, from any phase. */
	replay(&view, (const char *const[]) {"Hsuper + m", "BBegin chain", "LChain locked", "EEnd chain", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IDLE, ""));
	CHECK(banner_is_absent(&view));

	/* And a spurious E while idle is harmless. */
	replay(&view, (const char *const[]) {"EEnd chain", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IDLE, ""));
}

static void test_view_ignores_unknown_and_malformed(void)
{
	chain_view_t view;
	indicator_view_reset(&view);
	replay(&view, (const char *const[]) {"Xnew message", "", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IDLE, ""));

	replay(&view, (const char *const[]) {"Hsuper + m", "BBegin chain", "Xnew message", "", "Cwhatever", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IN_PROGRESS, "super + m"));
	CHECK(strcmp(banner_text_of(&view), "super + m ;") == 0);

	replay(&view, (const char *const[]) {"LChain locked", "Xnew message", "", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_LOCKED, "super + m"));

	/* An over-long H line is malformed, hence ignored, rather than truncated. */
	static char line[CHAIN_PROGRESS_CAPACITY + 2];
	line[0] = 'H';
	memset(line + 1, 'a', CHAIN_PROGRESS_CAPACITY);
	line[CHAIN_PROGRESS_CAPACITY + 1] = '\0';
	indicator_view_reset(&view);
	replay(&view, (const char *const[]) {line, "BBegin chain", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IN_PROGRESS, ""));
}

static void test_view_converges_after_dropped_lines(void)
{
	chain_view_t view;
	indicator_view_reset(&view);

	/* The E of a locked chain was dropped by a full pipe; the next chain
	 * still shows correctly from its second chord on and ends normally. */
	replay(&view, (const char *const[]) {"Hsuper + m", "BBegin chain", "LChain locked", NULL});
	replay(&view, (const char *const[]) {"Hsuper + n", "BBegin chain", NULL});
	CHECK(view.phase == CHAIN_PHASE_IN_PROGRESS);
	replay(&view, (const char *const[]) {"Hsuper + n;h", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IN_PROGRESS, "super + n;h"));
	replay(&view, (const char *const[]) {"EEnd chain", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_IDLE, ""));

	/* A lock seen without its B (joined mid-chain) still locks. */
	replay(&view, (const char *const[]) {"Hsuper + m", "LChain locked", NULL});
	CHECK(view_is(&view, CHAIN_PHASE_LOCKED, "super + m"));
}

/* The byte stream of the man page's plain chain, cut at odd places, ends
 * up as the same view as the line-by-line replay. */
static void test_stream_to_view(void)
{
	static indicator_line_buffer_t buffer;
	static collected_messages_t collected;
	chain_view_t view;
	indicator_line_buffer_reset(&buffer);
	indicator_view_reset(&view);

	const char *stream = "Hsuper + m\nBBegin chain\nHsuper + m;h\nEEnd chain\nCecho H\n";
	const size_t stream_length = strlen(stream);
	collected.count = 0;
	for (size_t offset = 0; offset < stream_length; offset += 7) {
		const size_t chunk_length = (stream_length - offset < 7) ? stream_length - offset : 7;
		indicator_line_buffer_feed(&buffer, stream + offset, chunk_length, collect_message, &collected);
	}
	CHECK(collected.count == 5);
	for (size_t index = 0; index < collected.count; index++) {
		indicator_view_apply(&view, &collected.messages[index]);
		if (index == 2)
			CHECK(strcmp(banner_text_of(&view), "super + m ; h ;") == 0);
	}
	CHECK(view_is(&view, CHAIN_PHASE_IDLE, ""));
	CHECK(banner_is_absent(&view));
}

int main(void)
{
	test_parse_every_prefix();
	test_parse_hotkey_text_bounds();
	test_parse_unknown_and_malformed();
	test_line_buffer_chunk_boundaries();
	test_line_buffer_overlong_line();
	test_view_plain_chain();
	test_view_single_chord_hotkey();
	test_view_locked_on_first_chord();
	test_view_locked_later();
	test_view_timeout_and_abort();
	test_view_reload_ends_locked_chain();
	test_view_ignores_unknown_and_malformed();
	test_view_converges_after_dropped_lines();
	test_stream_to_view();
	printf("indicator_protocol_test: %u passed, %u failed\n", passed_check_count, failed_check_count);
	return failed_check_count == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
