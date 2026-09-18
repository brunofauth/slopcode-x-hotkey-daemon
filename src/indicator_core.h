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

#ifndef SXHKD_INDICATOR_CORE_H
#define SXHKD_INDICATOR_CORE_H

/* The X-independent half of the on-screen chain indicator: configuration
 * types, command-line value parsers, banner text derivation and window
 * geometry. This header and its implementation deliberately include no X,
 * cairo or pango headers so that they can be compiled and unit-tested on a
 * machine without them (see test/indicator_core_test.c). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "helpers.h"
#include "chain_phase.h"

#define INDICATOR_DEFAULT_FONT_DESCRIPTION  "monospace 14"
#define INDICATOR_DEFAULT_FOREGROUND_COLOR  "#ffffff"
#define INDICATOR_DEFAULT_BACKGROUND_COLOR  "#222222"
#define INDICATOR_MARGIN_IN_PIXELS          16
#define INDICATOR_PADDING_IN_PIXELS         8

/* The chain progress string holds at most 3 * MAXLEN - 1 = 767 bytes and thus
 * at most 383 separators. Rendering expands each separator to " ; " (+2 bytes
 * each), appends a 2-byte pending-chord marker and a NUL:
 * 767 + 2 * 383 + 2 + 1 = 1536 <= 2304. The writer is bounded regardless. */
#define INDICATOR_TEXT_CAPACITY             (3 * (3 * MAXLEN))

typedef enum {
	INDICATOR_POSITION_TOP,
	INDICATOR_POSITION_TOP_LEFT,
	INDICATOR_POSITION_TOP_RIGHT,
	INDICATOR_POSITION_BOTTOM,
	INDICATOR_POSITION_BOTTOM_LEFT,
	INDICATOR_POSITION_BOTTOM_RIGHT,
	INDICATOR_POSITION_CENTER,
	INDICATOR_POSITION_CENTER_LEFT,
	INDICATOR_POSITION_CENTER_RIGHT
} indicator_position_t;

typedef struct {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t alpha;   /* 0xff: opaque */
} rgba_color_t;

typedef struct {
	indicator_position_t position;
	/* Points into argv[] or at a string literal: valid for the process lifetime. */
	const char *font_description_text;
	rgba_color_t foreground_color;
	rgba_color_t background_color;
} indicator_config_t;

typedef enum {
	INDICATOR_SETTINGS_DISABLED,
	INDICATOR_SETTINGS_ENABLED
} indicator_settings_kind_t;

typedef struct {
	indicator_settings_kind_t kind;
	union {
		indicator_config_t enabled;   /* valid iff kind == INDICATOR_SETTINGS_ENABLED */
	} as;
} indicator_settings_t;

typedef enum {
	INDICATOR_BANNER_ABSENT,
	INDICATOR_BANNER_PRESENT
} indicator_banner_kind_t;

typedef struct {
	indicator_banner_kind_t kind;
	char text[INDICATOR_TEXT_CAPACITY];   /* meaningful iff kind == INDICATOR_BANNER_PRESENT */
} indicator_banner_t;

typedef struct {
	uint16_t width;
	uint16_t height;
} pixel_size_t;

typedef struct {
	int16_t x;
	int16_t y;
} pixel_origin_t;

typedef struct {
	const char *name;
	indicator_position_t position;
} indicator_position_name_t;

/* Every accepted position name, in display order: the single source for the
 * parser, the help text and the diagnostics. */
extern const indicator_position_name_t indicator_position_names[];
extern const size_t indicator_position_name_count;

bool indicator_parse_position(const char *position_text, indicator_position_t *parsed_position);
/* Accepts "#rrggbb" (opaque) and "#rrggbbaa"; never writes on failure. */
bool indicator_parse_rgba_color(const char *color_text, rgba_color_t *parsed_color);
bool rgba_color_is_translucent(rgba_color_t color);
void indicator_derive_banner(chain_phase_t chain_phase, const char *progress_text, indicator_banner_t *banner);
uint16_t indicator_pixel_extent_from_int(int32_t extent);
pixel_origin_t indicator_compute_window_origin(indicator_position_t position, pixel_size_t screen_size, pixel_size_t window_size, uint16_t margin_in_pixels);

#endif
