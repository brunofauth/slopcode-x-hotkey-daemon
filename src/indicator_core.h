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
#include "chain_phase.h"
/* For the modules built on this core: this one reports nothing itself. */
#include "diagnostics.h"

#define INDICATOR_DEFAULT_FONT_DESCRIPTION  "monospace 14"
#define INDICATOR_DEFAULT_FOREGROUND_COLOR  "#ffffff"
#define INDICATOR_DEFAULT_BACKGROUND_COLOR  "#222222"
#define INDICATOR_MARGIN_IN_PIXELS          16
#define INDICATOR_PADDING_IN_PIXELS         8

/* The chain progress string holds at most CHAIN_PROGRESS_CAPACITY - 1 bytes.
 * Rendering keeps every byte or trims it, except each separator, which it
 * expands to " ; " (+2 bytes), then appends a 2-byte pending-chord marker and
 * a NUL. The worst case is a string made only of separators (chord texts may
 * be empty): 3 * (CHAIN_PROGRESS_CAPACITY - 1) + 2 + 1 = 3 * CHAIN_PROGRESS_CAPACITY,
 * which this capacity holds exactly. The writer is bounded regardless. */
#define INDICATOR_TEXT_CAPACITY             (3 * CHAIN_PROGRESS_CAPACITY)

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
/* The same color with a fully opaque alpha. Used where no alpha channel is
 * available, so that the user sees the color they typed rather than the
 * darker premultiplied one cairo would produce from a translucent source. */
rgba_color_t rgba_color_forced_opaque(rgba_color_t color);
void indicator_derive_banner(chain_phase_t chain_phase, const char *progress_text, indicator_banner_t *banner);
uint16_t indicator_pixel_extent_from_int(int32_t extent);
pixel_origin_t indicator_compute_window_origin(indicator_position_t position, pixel_size_t screen_size, pixel_size_t window_size, uint16_t margin_in_pixels);

#endif
