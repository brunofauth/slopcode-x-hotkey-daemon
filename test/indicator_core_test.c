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

/* Headless unit test for src/indicator_core.c. Built and run by `make check`;
 * needs no X server, cairo or pango. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "indicator_core.h"

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

static void test_parse_position(void)
{
	indicator_position_t position = INDICATOR_POSITION_CENTER;
	CHECK(indicator_parse_position("top", &position) && position == INDICATOR_POSITION_TOP);
	CHECK(indicator_parse_position("top-left", &position) && position == INDICATOR_POSITION_TOP_LEFT);
	CHECK(indicator_parse_position("top-right", &position) && position == INDICATOR_POSITION_TOP_RIGHT);
	CHECK(indicator_parse_position("bottom", &position) && position == INDICATOR_POSITION_BOTTOM);
	CHECK(indicator_parse_position("bottom-left", &position) && position == INDICATOR_POSITION_BOTTOM_LEFT);
	CHECK(indicator_parse_position("bottom-right", &position) && position == INDICATOR_POSITION_BOTTOM_RIGHT);
	CHECK(indicator_parse_position("center", &position) && position == INDICATOR_POSITION_CENTER);
	CHECK(indicator_parse_position("center-left", &position) && position == INDICATOR_POSITION_CENTER_LEFT);
	CHECK(indicator_parse_position("center-right", &position) && position == INDICATOR_POSITION_CENTER_RIGHT);

	position = INDICATOR_POSITION_BOTTOM_LEFT;
	CHECK(!indicator_parse_position("TOP-LEFT", &position));
	CHECK(!indicator_parse_position("top_left", &position));
	CHECK(!indicator_parse_position("top-left ", &position));
	CHECK(!indicator_parse_position("", &position));
	CHECK(!indicator_parse_position("centre", &position));
	CHECK(position == INDICATOR_POSITION_BOTTOM_LEFT);   /* untouched on failure */
}

static void test_parse_rgb_color(void)
{
	rgba_color_t color = {1, 2, 3, 4};
	CHECK(indicator_parse_rgba_color("#000000", &color) && color.red == 0 && color.green == 0 && color.blue == 0);
	CHECK(indicator_parse_rgba_color("#FfFfFf", &color) && color.red == 255 && color.green == 255 && color.blue == 255);
	CHECK(indicator_parse_rgba_color("#123abc", &color) && color.red == 0x12 && color.green == 0x3a && color.blue == 0xbc);
	CHECK(indicator_parse_rgba_color(INDICATOR_DEFAULT_FOREGROUND_COLOR, &color));
	CHECK(indicator_parse_rgba_color(INDICATOR_DEFAULT_BACKGROUND_COLOR, &color));
	CHECK(indicator_parse_rgba_color("#123456", &color) && color.alpha == 0xff);
	CHECK(indicator_parse_rgba_color("#22222280", &color) && color.red == 0x22 && color.green == 0x22 && color.blue == 0x22 && color.alpha == 0x80);
	CHECK(indicator_parse_rgba_color("#FFFFFFFF", &color) && color.alpha == 0xff);
	CHECK(indicator_parse_rgba_color("#01020300", &color) && color.red == 1 && color.green == 2 && color.blue == 3 && color.alpha == 0);
	CHECK(indicator_parse_rgba_color(INDICATOR_DEFAULT_FOREGROUND_COLOR, &color) && !rgba_color_is_translucent(color));
	CHECK(indicator_parse_rgba_color(INDICATOR_DEFAULT_BACKGROUND_COLOR, &color) && !rgba_color_is_translucent(color));
	CHECK(indicator_parse_rgba_color("#000000fe", &color) && rgba_color_is_translucent(color));

	color.red = 7;
	color.green = 8;
	color.blue = 9;
	color.alpha = 10;
	CHECK(!indicator_parse_rgba_color("123456", &color));
	CHECK(!indicator_parse_rgba_color("#12345", &color));
	CHECK(!indicator_parse_rgba_color("#1234567", &color));
	CHECK(!indicator_parse_rgba_color("#12345g", &color));
	CHECK(!indicator_parse_rgba_color("#-12345", &color));
	CHECK(!indicator_parse_rgba_color("# 12345", &color));
	CHECK(!indicator_parse_rgba_color("#0x1234", &color));
	CHECK(!indicator_parse_rgba_color("", &color));
	CHECK(!indicator_parse_rgba_color("#", &color));
	CHECK(!indicator_parse_rgba_color("#123456789", &color));
	CHECK(!indicator_parse_rgba_color("#1234567g", &color));
	CHECK(!indicator_parse_rgba_color("#abc", &color));
	CHECK(!indicator_parse_rgba_color("#abcd", &color));
	CHECK(color.red == 7 && color.green == 8 && color.blue == 9 && color.alpha == 10);   /* untouched on failure */
}

static void test_derive_banner(void)
{
	static indicator_banner_t banner;

	memset(&banner, 0x55, sizeof(banner));
	indicator_derive_banner(CHAIN_PHASE_IDLE, "super + m", &banner);
	CHECK(banner.kind == INDICATOR_BANNER_ABSENT);
	CHECK(banner.text[0] == '\0');

	indicator_derive_banner(CHAIN_PHASE_IN_PROGRESS, "super + m", &banner);
	CHECK(banner.kind == INDICATOR_BANNER_PRESENT);
	CHECK(strcmp(banner.text, "super + m ;") == 0);

	indicator_derive_banner(CHAIN_PHASE_IN_PROGRESS, "super + m;h", &banner);
	CHECK(banner.kind == INDICATOR_BANNER_PRESENT);
	CHECK(strcmp(banner.text, "super + m ; h ;") == 0);

	indicator_derive_banner(CHAIN_PHASE_LOCKED, "super + n", &banner);
	CHECK(banner.kind == INDICATOR_BANNER_PRESENT);
	CHECK(strcmp(banner.text, "super + n :") == 0);

	indicator_derive_banner(CHAIN_PHASE_LOCKED, "super + n;h;j", &banner);
	CHECK(strcmp(banner.text, "super + n ; h ; j :") == 0);

	/* Raw chord tokens keep the blanks around their separators; the banner must not. */
	indicator_derive_banner(CHAIN_PHASE_IN_PROGRESS, "super + m ; h", &banner);
	CHECK(strcmp(banner.text, "super + m ; h ;") == 0);
	indicator_derive_banner(CHAIN_PHASE_LOCKED, "  super + n  ;\t h ;j  ", &banner);
	CHECK(strcmp(banner.text, "super + n ; h ; j :") == 0);
	indicator_derive_banner(CHAIN_PHASE_IN_PROGRESS, "super  +  m", &banner);
	CHECK(strcmp(banner.text, "super  +  m ;") == 0);   /* inner blanks are the user's */

	indicator_derive_banner(CHAIN_PHASE_IN_PROGRESS, "", &banner);
	CHECK(banner.kind == INDICATOR_BANNER_PRESENT);
	CHECK(strcmp(banner.text, " ;") == 0);

	/* The longest possible progress string: 3 * MAXLEN - 1 bytes of "a;a;...;a". */
	static char longest_progress[3 * MAXLEN];
	const size_t longest_progress_length = sizeof(longest_progress) - 1;
	for (size_t index = 0; index < longest_progress_length; index++)
		longest_progress[index] = (index % 2 == 0) ? 'a' : CHAIN_PROGRESS_SEPARATOR;
	longest_progress[longest_progress_length] = '\0';
	const size_t separator_count = longest_progress_length / 2;
	const size_t expected_length = longest_progress_length + 2 * separator_count + 2;

	indicator_derive_banner(CHAIN_PHASE_IN_PROGRESS, longest_progress, &banner);
	CHECK(banner.kind == INDICATOR_BANNER_PRESENT);
	CHECK(strlen(banner.text) == expected_length);
	CHECK(expected_length < INDICATOR_TEXT_CAPACITY);
	CHECK(strncmp(banner.text, "a ; a ; a", 9) == 0);
	CHECK(strcmp(banner.text + expected_length - 7, "a ; a ;") == 0);
}

static void test_pixel_extent_from_int(void)
{
	CHECK(indicator_pixel_extent_from_int(-5) == 1);
	CHECK(indicator_pixel_extent_from_int(0) == 1);
	CHECK(indicator_pixel_extent_from_int(1) == 1);
	CHECK(indicator_pixel_extent_from_int(1234) == 1234);
	CHECK(indicator_pixel_extent_from_int(65535) == 65535);
	CHECK(indicator_pixel_extent_from_int(65536) == 65535);
	CHECK(indicator_pixel_extent_from_int(INT32_MAX) == 65535);
}

static void test_compute_window_origin(void)
{
	const pixel_size_t screen_size = {1280, 720};
	const pixel_size_t window_size = {200, 40};
	const uint16_t margin = 16;
	pixel_origin_t origin;

	origin = indicator_compute_window_origin(INDICATOR_POSITION_TOP_LEFT, screen_size, window_size, margin);
	CHECK(origin.x == 16 && origin.y == 16);
	origin = indicator_compute_window_origin(INDICATOR_POSITION_TOP_RIGHT, screen_size, window_size, margin);
	CHECK(origin.x == 1280 - 200 - 16 && origin.y == 16);
	origin = indicator_compute_window_origin(INDICATOR_POSITION_BOTTOM_LEFT, screen_size, window_size, margin);
	CHECK(origin.x == 16 && origin.y == 720 - 40 - 16);
	origin = indicator_compute_window_origin(INDICATOR_POSITION_BOTTOM_RIGHT, screen_size, window_size, margin);
	CHECK(origin.x == 1280 - 200 - 16 && origin.y == 720 - 40 - 16);
	origin = indicator_compute_window_origin(INDICATOR_POSITION_CENTER, screen_size, window_size, margin);
	CHECK(origin.x == (1280 - 200) / 2 && origin.y == (720 - 40) / 2);
	origin = indicator_compute_window_origin(INDICATOR_POSITION_TOP, screen_size, window_size, margin);
	CHECK(origin.x == (1280 - 200) / 2 && origin.y == 16);
	origin = indicator_compute_window_origin(INDICATOR_POSITION_BOTTOM, screen_size, window_size, margin);
	CHECK(origin.x == (1280 - 200) / 2 && origin.y == 720 - 40 - 16);
	origin = indicator_compute_window_origin(INDICATOR_POSITION_CENTER_LEFT, screen_size, window_size, margin);
	CHECK(origin.x == 16 && origin.y == (720 - 40) / 2);
	origin = indicator_compute_window_origin(INDICATOR_POSITION_CENTER_RIGHT, screen_size, window_size, margin);
	CHECK(origin.x == 1280 - 200 - 16 && origin.y == (720 - 40) / 2);

	/* A window wider and taller than the screen is pinned to the origin. */
	const pixel_size_t oversized_window = {2000, 1000};
	origin = indicator_compute_window_origin(INDICATOR_POSITION_BOTTOM_RIGHT, screen_size, oversized_window, margin);
	CHECK(origin.x == 0 && origin.y == 0);
	origin = indicator_compute_window_origin(INDICATOR_POSITION_CENTER, screen_size, oversized_window, margin);
	CHECK(origin.x == 0 && origin.y == 0);
	origin = indicator_compute_window_origin(INDICATOR_POSITION_CENTER_RIGHT, screen_size, oversized_window, margin);
	CHECK(origin.x == 0 && origin.y == 0);

	/* Coordinates beyond the X protocol's int16 range are clamped, never wrapped. */
	const pixel_size_t huge_screen = {65535, 65535};
	const pixel_size_t tiny_window = {1, 1};
	origin = indicator_compute_window_origin(INDICATOR_POSITION_BOTTOM_RIGHT, huge_screen, tiny_window, 0);
	CHECK(origin.x == INT16_MAX && origin.y == INT16_MAX);
	origin = indicator_compute_window_origin(INDICATOR_POSITION_TOP_LEFT, huge_screen, tiny_window, 65535);
	CHECK(origin.x == INT16_MAX && origin.y == INT16_MAX);
}

int main(void)
{
	test_parse_position();
	test_parse_rgb_color();
	test_derive_banner();
	test_pixel_extent_from_int();
	test_compute_window_origin();
	printf("indicator_core_test: %u passed, %u failed\n", passed_check_count, failed_check_count);
	return failed_check_count == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
