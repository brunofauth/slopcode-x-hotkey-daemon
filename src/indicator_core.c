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

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "indicator_core.h"

bool indicator_parse_position(const char *position_text, indicator_position_t *parsed_position)
{
	static const struct {
		const char *name;
		indicator_position_t position;
	} known_positions[] = {
		{"top", INDICATOR_POSITION_TOP},
		{"top-left", INDICATOR_POSITION_TOP_LEFT},
		{"top-right", INDICATOR_POSITION_TOP_RIGHT},
		{"bottom", INDICATOR_POSITION_BOTTOM},
		{"bottom-left", INDICATOR_POSITION_BOTTOM_LEFT},
		{"bottom-right", INDICATOR_POSITION_BOTTOM_RIGHT},
		{"center", INDICATOR_POSITION_CENTER},
		{"center-left", INDICATOR_POSITION_CENTER_LEFT},
		{"center-right", INDICATOR_POSITION_CENTER_RIGHT},
	};
	for (size_t index = 0; index < LENGTH(known_positions); index++) {
		if (strcmp(position_text, known_positions[index].name) == 0) {
			*parsed_position = known_positions[index].position;
			return true;
		}
	}
	return false;
}

bool indicator_parse_rgb_color(const char *color_text, rgb_color_t *parsed_color)
{
	const size_t hex_digit_count = 6;
	if (strlen(color_text) != hex_digit_count + 1 || color_text[0] != '#')
		return false;
	for (size_t index = 1; index <= hex_digit_count; index++) {
		if (!isxdigit((unsigned char) color_text[index]))
			return false;
	}
	/* Every remaining byte is a hex digit, so strtoul cannot see a sign, blank
	 * or "0x" prefix; the checks below are belt and braces. */
	const char *hex_digits = color_text + 1;
	char *end_of_number = NULL;
	errno = 0;
	const unsigned long packed_color = strtoul(hex_digits, &end_of_number, 16);
	if (errno != 0 || end_of_number != hex_digits + hex_digit_count || packed_color > 0xFFFFFFUL)
		return false;
	parsed_color->red = (uint8_t) ((packed_color >> 16) & 0xFFUL);
	parsed_color->green = (uint8_t) ((packed_color >> 8) & 0xFFUL);
	parsed_color->blue = (uint8_t) (packed_color & 0xFFUL);
	return true;
}

/* A writer that never steps past the end of its buffer and always leaves it
 * NUL-terminated, even if asked to write more than fits. */
typedef struct {
	char *buffer;
	size_t capacity;
	size_t length;
} bounded_writer_t;

static void bounded_writer_append_character(bounded_writer_t *writer, char character)
{
	if (writer->length + 1 < writer->capacity) {
		writer->buffer[writer->length] = character;
		writer->length++;
	}
	writer->buffer[writer->length] = '\0';
}

static void bounded_writer_append(bounded_writer_t *writer, const char *text)
{
	for (const char *cursor = text; *cursor != '\0'; cursor++)
		bounded_writer_append_character(writer, *cursor);
}

/* Appends [chord_begin, chord_end) without its surrounding blanks. Chord texts
 * are raw configuration tokens and keep the blanks that surrounded their
 * separator, e.g. "super + m " and " h" for "super + m ; h". */
static bool is_blank_byte(char byte)
{
	/* Deliberately not isblank(): locale-independent, and never trims the
	 * bytes of a multi-byte character. */
	return byte == ' ' || byte == '\t';
}

static void bounded_writer_append_trimmed(bounded_writer_t *writer, const char *chord_begin, const char *chord_end)
{
	while (chord_begin < chord_end && is_blank_byte(*chord_begin))
		chord_begin++;
	while (chord_end > chord_begin && is_blank_byte(chord_end[-1]))
		chord_end--;
	for (const char *cursor = chord_begin; cursor < chord_end; cursor++)
		bounded_writer_append_character(writer, *cursor);
}

void indicator_derive_banner(chain_phase_t chain_phase, const char *progress_text, indicator_banner_t *banner)
{
	banner->kind = INDICATOR_BANNER_ABSENT;
	banner->text[0] = '\0';

	/* The trailing marker mirrors the configuration syntax: ';' means the
	 * chain is waiting for its next chord, ':' means it is locked. */
	const char *pending_chord_marker = NULL;
	switch (chain_phase) {
		case CHAIN_PHASE_IDLE:
			return;
		case CHAIN_PHASE_IN_PROGRESS:
			pending_chord_marker = " ;";
			break;
		case CHAIN_PHASE_LOCKED:
			pending_chord_marker = " :";
			break;
	}
	if (pending_chord_marker == NULL)
		return;

	bounded_writer_t writer = {banner->text, sizeof(banner->text), 0};
	const char *chord_begin = progress_text;
	for (;;) {
		const char *chord_end = chord_begin;
		while (*chord_end != '\0' && *chord_end != CHAIN_PROGRESS_SEPARATOR)
			chord_end++;
		bounded_writer_append_trimmed(&writer, chord_begin, chord_end);
		if (*chord_end == '\0')
			break;
		bounded_writer_append(&writer, " ; ");
		chord_begin = chord_end + 1;
	}
	bounded_writer_append(&writer, pending_chord_marker);
	banner->kind = INDICATOR_BANNER_PRESENT;
}

uint16_t indicator_pixel_extent_from_int(int32_t extent)
{
	if (extent < 1)
		return 1;
	if (extent > UINT16_MAX)
		return UINT16_MAX;
	return (uint16_t) extent;
}

static int16_t clamp_to_screen_coordinate(int32_t coordinate)
{
	if (coordinate < 0)
		return 0;
	if (coordinate > INT16_MAX)
		return INT16_MAX;
	return (int16_t) coordinate;
}

pixel_origin_t indicator_compute_window_origin(indicator_position_t position, pixel_size_t screen_size, pixel_size_t window_size, uint16_t margin_in_pixels)
{
	/* All arithmetic happens in int32_t, which holds every intermediate value
	 * of two uint16_t operands without overflow. */
	const int32_t screen_width = screen_size.width;
	const int32_t screen_height = screen_size.height;
	const int32_t window_width = window_size.width;
	const int32_t window_height = window_size.height;
	const int32_t margin = margin_in_pixels;

	const int32_t left_aligned_x = margin;
	const int32_t right_aligned_x = screen_width - window_width - margin;
	const int32_t centered_x = (screen_width - window_width) / 2;
	const int32_t top_aligned_y = margin;
	const int32_t bottom_aligned_y = screen_height - window_height - margin;
	const int32_t centered_y = (screen_height - window_height) / 2;

	int32_t x = 0;
	int32_t y = 0;
	switch (position) {
		case INDICATOR_POSITION_TOP:
			x = centered_x;
			y = top_aligned_y;
			break;
		case INDICATOR_POSITION_TOP_LEFT:
			x = left_aligned_x;
			y = top_aligned_y;
			break;
		case INDICATOR_POSITION_TOP_RIGHT:
			x = right_aligned_x;
			y = top_aligned_y;
			break;
		case INDICATOR_POSITION_BOTTOM:
			x = centered_x;
			y = bottom_aligned_y;
			break;
		case INDICATOR_POSITION_BOTTOM_LEFT:
			x = left_aligned_x;
			y = bottom_aligned_y;
			break;
		case INDICATOR_POSITION_BOTTOM_RIGHT:
			x = right_aligned_x;
			y = bottom_aligned_y;
			break;
		case INDICATOR_POSITION_CENTER:
			x = centered_x;
			y = centered_y;
			break;
		case INDICATOR_POSITION_CENTER_LEFT:
			x = left_aligned_x;
			y = centered_y;
			break;
		case INDICATOR_POSITION_CENTER_RIGHT:
			x = right_aligned_x;
			y = centered_y;
			break;
	}
	const pixel_origin_t origin = {clamp_to_screen_coordinate(x), clamp_to_screen_coordinate(y)};
	return origin;
}
