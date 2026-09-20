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

#include <string.h>
#include "indicator_protocol.h"

/* The prefix letters of the protocol. Kept next to their parser rather than
 * shared with the daemon: the two programs must agree on the wire format,
 * which the man page fixes, not on a header. */
#define HOTKEY_PREFIX  'H'
#define BEGIN_PREFIX   'B'
#define LOCKED_PREFIX  'L'
#define ABORTED_PREFIX 'A'
#define END_PREFIX     'E'
#define TIMEOUT_PREFIX 'T'
#define COMMAND_PREFIX 'C'

void indicator_parse_status_line(const char *line, size_t length, status_message_t *parsed)
{
	/* A NUL byte would silently cut the text at the C string level; nothing
	 * the daemon writes contains one. */
	if (length == 0 || memchr(line, '\0', length) != NULL) {
		parsed->kind = STATUS_MESSAGE_MALFORMED;
		return;
	}
	const char prefix = line[0];
	const char *text = line + 1;
	const size_t text_length = length - 1;
	switch (prefix) {
		case HOTKEY_PREFIX:
			if (text_length >= sizeof(parsed->as.hotkey.text)) {
				parsed->kind = STATUS_MESSAGE_MALFORMED;
				return;
			}
			parsed->kind = STATUS_MESSAGE_HOTKEY;
			memcpy(parsed->as.hotkey.text, text, text_length);
			parsed->as.hotkey.text[text_length] = '\0';
			return;
		case BEGIN_PREFIX:
			parsed->kind = STATUS_MESSAGE_BEGIN;
			return;
		case LOCKED_PREFIX:
			parsed->kind = STATUS_MESSAGE_LOCKED;
			return;
		case ABORTED_PREFIX:
			parsed->kind = STATUS_MESSAGE_ABORTED;
			return;
		case END_PREFIX:
			parsed->kind = STATUS_MESSAGE_END;
			return;
		case TIMEOUT_PREFIX:
			parsed->kind = STATUS_MESSAGE_TIMEOUT;
			return;
		case COMMAND_PREFIX:
			parsed->kind = STATUS_MESSAGE_COMMAND;
			return;
		default:
			parsed->kind = STATUS_MESSAGE_UNKNOWN;
			parsed->as.unknown.prefix = prefix;
			return;
	}
}

void indicator_line_buffer_reset(indicator_line_buffer_t *buffer)
{
	buffer->state = LINE_BUFFER_COLLECTING;
	buffer->length = 0;
}

/* Parses and delivers the line held, then empties the buffer. */
static void deliver_held_line(indicator_line_buffer_t *buffer, status_message_callback_t on_message, void *context)
{
	/* Static: the message is the size of the progress string, and this
	 * module is single-threaded and never re-entered from the callback. */
	static status_message_t message;
	indicator_parse_status_line(buffer->bytes, buffer->length, &message);
	buffer->length = 0;
	on_message(&message, context);
}

static void deliver_malformed(status_message_callback_t on_message, void *context)
{
	static const status_message_t malformed = {.kind = STATUS_MESSAGE_MALFORMED};
	on_message(&malformed, context);
}

void indicator_line_buffer_feed(indicator_line_buffer_t *buffer, const char *bytes, size_t byte_count, status_message_callback_t on_message, void *context)
{
	for (size_t index = 0; index < byte_count; index++) {
		const char byte = bytes[index];
		switch (buffer->state) {
			case LINE_BUFFER_COLLECTING:
				if (byte == '\n') {
					deliver_held_line(buffer, on_message, context);
					break;
				}
				if (buffer->length == sizeof(buffer->bytes)) {
					/* Reported at the moment the line overflows, so that the
					 * report cannot be lost if its end never arrives. */
					buffer->state = LINE_BUFFER_DISCARDING;
					buffer->length = 0;
					deliver_malformed(on_message, context);
					break;
				}
				buffer->bytes[buffer->length] = byte;
				buffer->length++;
				break;
			case LINE_BUFFER_DISCARDING:
				if (byte == '\n')
					buffer->state = LINE_BUFFER_COLLECTING;
				break;
		}
	}
}

void indicator_view_reset(chain_view_t *view)
{
	view->phase = CHAIN_PHASE_IDLE;
	view->progress[0] = '\0';
}

/* The H text is bounded like the progress string, so the copy always fits. */
static void store_progress(chain_view_t *view, const char *text)
{
	memcpy(view->progress, text, strlen(text) + 1);
}

void indicator_view_apply(chain_view_t *view, const status_message_t *message)
{
	switch (message->kind) {
		case STATUS_MESSAGE_HOTKEY:
			switch (view->phase) {
				case CHAIN_PHASE_IDLE:
				case CHAIN_PHASE_IN_PROGRESS:
					store_progress(view, message->as.hotkey.text);
					break;
				case CHAIN_PHASE_LOCKED:
					break;
			}
			break;
		case STATUS_MESSAGE_BEGIN:
			switch (view->phase) {
				case CHAIN_PHASE_IDLE:
				case CHAIN_PHASE_IN_PROGRESS:
				case CHAIN_PHASE_LOCKED:
					view->phase = CHAIN_PHASE_IN_PROGRESS;
					break;
			}
			break;
		case STATUS_MESSAGE_LOCKED:
			switch (view->phase) {
				case CHAIN_PHASE_IDLE:
				case CHAIN_PHASE_IN_PROGRESS:
				case CHAIN_PHASE_LOCKED:
					view->phase = CHAIN_PHASE_LOCKED;
					break;
			}
			break;
		case STATUS_MESSAGE_END:
			switch (view->phase) {
				case CHAIN_PHASE_IDLE:
				case CHAIN_PHASE_IN_PROGRESS:
				case CHAIN_PHASE_LOCKED:
					indicator_view_reset(view);
					break;
			}
			break;
		case STATUS_MESSAGE_ABORTED:
		case STATUS_MESSAGE_TIMEOUT:
		case STATUS_MESSAGE_COMMAND:
		case STATUS_MESSAGE_UNKNOWN:
		case STATUS_MESSAGE_MALFORMED:
			break;
	}
}
