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

#include <cairo.h>
#include <cairo-xcb.h>
#include <glib.h>
#include <pango/pangocairo.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/shape.h>
#include <xcb/xcb.h>
#include "indicator.h"

/* Everything the enabled indicator owns. Exists iff the indicator is enabled. */
typedef struct {
	xcb_connection_t *connection;
	xcb_window_t root_window;
	xcb_window_t window;
	cairo_device_t *device;
	cairo_surface_t *surface;
	cairo_t *cairo_context;
	PangoLayout *layout;
	PangoFontDescription *font_description;
	indicator_config_t config;
	pixel_size_t screen_size;
	/* The size last passed to cairo_xcb_surface_set_size(); cairo-xcb does
	 * not track the drawable's size by itself. */
	pixel_size_t surface_size;
} indicator_resources_t;

typedef enum {
	INDICATOR_DISPLAY_HIDDEN,
	INDICATOR_DISPLAY_VISIBLE
} indicator_display_kind_t;

/* What is currently on screen. The cached text exists iff the banner is
 * visible, so an Expose can always be answered from it. */
typedef struct {
	indicator_display_kind_t kind;
	union {
		struct {
			chain_phase_t chain_phase;
			char text[INDICATOR_TEXT_CAPACITY];
		} visible;
	} as;
} indicator_display_t;

typedef struct {
	indicator_resources_t resources;
	indicator_display_t display;
} indicator_enabled_t;

typedef enum {
	INDICATOR_DISABLED,
	INDICATOR_ENABLED
} indicator_kind_t;

typedef struct {
	indicator_kind_t kind;
	union {
		indicator_enabled_t enabled;
	} as;
} indicator_t;

static indicator_t indicator_singleton = {.kind = INDICATOR_DISABLED};

static xcb_visualtype_t *find_visual_type(const xcb_screen_t *screen_of_window, xcb_visualid_t wanted_visual_id)
{
	xcb_depth_iterator_t depth_iterator = xcb_screen_allowed_depths_iterator(screen_of_window);
	for (; depth_iterator.rem > 0; xcb_depth_next(&depth_iterator)) {
		xcb_visualtype_iterator_t visual_iterator = xcb_depth_visuals_iterator(depth_iterator.data);
		for (; visual_iterator.rem > 0; xcb_visualtype_next(&visual_iterator)) {
			if (visual_iterator.data->visual_id == wanted_visual_id)
				return visual_iterator.data;
		}
	}
	return NULL;
}

/* Places an 8-bit color component into the bits selected by a channel mask of
 * a TrueColor/DirectColor visual, rescaling it to the channel's width. */
static uint32_t scale_component_into_channel(uint8_t component, uint32_t channel_mask)
{
	if (channel_mask == 0)
		return 0;
	unsigned int channel_shift = 0;
	while (((channel_mask >> channel_shift) & 1u) == 0)
		channel_shift++;   /* terminates: channel_mask != 0, hence channel_shift < 32 */
	unsigned int channel_width = 0;
	while (channel_shift + channel_width < 32 && ((channel_mask >> (channel_shift + channel_width)) & 1u) == 1)
		channel_width++;
	const uint32_t component_value = component;
	const uint32_t scaled_value = (channel_width >= 8)
		? (component_value << (channel_width - 8))
		: (component_value >> (8 - channel_width));
	return (scaled_value << channel_shift) & channel_mask;
}

/* The pixel value used as the window's background attribute, so that the
 * server never shows garbage between mapping and the first paint. cairo
 * paints the real colors afterwards, so a fallback to black is harmless. */
static uint32_t pixel_from_rgb_color(const xcb_visualtype_t *visual_type, const xcb_screen_t *screen_of_window, rgb_color_t color)
{
	if (visual_type->_class != XCB_VISUAL_CLASS_TRUE_COLOR && visual_type->_class != XCB_VISUAL_CLASS_DIRECT_COLOR)
		return screen_of_window->black_pixel;
	return scale_component_into_channel(color.red, visual_type->red_mask)
		| scale_component_into_channel(color.green, visual_type->green_mask)
		| scale_component_into_channel(color.blue, visual_type->blue_mask);
}

/* With the Shape extension, an empty input region lets pointer events fall
 * through the banner to whatever lies beneath it. Without the extension the
 * banner merely swallows clicks on its own small area. */
static void make_window_transparent_to_input(xcb_connection_t *connection, xcb_window_t window)
{
	const xcb_query_extension_reply_t *shape_extension = xcb_get_extension_data(connection, &xcb_shape_id);
	if (shape_extension == NULL || !shape_extension->present)
		return;
	xcb_shape_rectangles(connection, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT, XCB_CLIP_ORDERING_UNSORTED, window, 0, 0, 0, NULL);
}

static int available_text_width_in_pango_units(pixel_size_t screen_size)
{
	int32_t available_width_in_pixels = (int32_t) screen_size.width - 2 * INDICATOR_MARGIN_IN_PIXELS - 2 * INDICATOR_PADDING_IN_PIXELS;
	if (available_width_in_pixels < 1)
		available_width_in_pixels = 1;
	/* At most 65535 * 1024, which fits comfortably in an int32_t. */
	return (int) (available_width_in_pixels * PANGO_SCALE);
}

/* Pango requires valid UTF-8; the text ultimately comes from the user's
 * configuration file, which is not guaranteed to be. */
static void set_layout_text_safely(PangoLayout *layout, const char *text)
{
	if (g_utf8_validate(text, -1, NULL)) {
		pango_layout_set_text(layout, text, -1);
		return;
	}
	gchar *sanitized_text = g_utf8_make_valid(text, -1);
	pango_layout_set_text(layout, sanitized_text, -1);
	g_free(sanitized_text);
}

static void set_source_from_rgb_color(cairo_t *cairo_context, rgb_color_t color)
{
	cairo_set_source_rgb(cairo_context, color.red / 255.0, color.green / 255.0, color.blue / 255.0);
}

static cairo_status_t paint_banner(const indicator_resources_t *resources)
{
	cairo_t *cairo_context = resources->cairo_context;
	set_source_from_rgb_color(cairo_context, resources->config.background_color);
	cairo_paint(cairo_context);
	set_source_from_rgb_color(cairo_context, resources->config.foreground_color);
	cairo_move_to(cairo_context, INDICATOR_PADDING_IN_PIXELS, INDICATOR_PADDING_IN_PIXELS);
	pango_cairo_show_layout(cairo_context, resources->layout);
	cairo_surface_flush(resources->surface);
	xcb_flush(resources->connection);
	const cairo_status_t context_status = cairo_status(cairo_context);
	if (context_status != CAIRO_STATUS_SUCCESS)
		return context_status;
	return cairo_surface_status(resources->surface);
}

/* Measures the layout's current text, sizes and places the window for it,
 * maps the window if it is hidden and paints. Does not touch the cache. */
static cairo_status_t place_and_paint_banner(indicator_enabled_t *enabled)
{
	indicator_resources_t *resources = &enabled->resources;

	int text_width = 0;
	int text_height = 0;
	pango_layout_get_pixel_size(resources->layout, &text_width, &text_height);
	const pixel_size_t window_size = {
		indicator_pixel_extent_from_int(text_width + 2 * INDICATOR_PADDING_IN_PIXELS),
		indicator_pixel_extent_from_int(text_height + 2 * INDICATOR_PADDING_IN_PIXELS),
	};
	const pixel_origin_t origin = indicator_compute_window_origin(resources->config.position, resources->screen_size, window_size, INDICATOR_MARGIN_IN_PIXELS);

	/* Values must follow the ascending bit order of their mask flags. */
	const uint16_t configure_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_STACK_MODE;
	const uint32_t configure_values[] = {
		(uint32_t) origin.x,   /* never negative: clamped to [0, INT16_MAX] */
		(uint32_t) origin.y,
		window_size.width,
		window_size.height,
		XCB_STACK_MODE_ABOVE,  /* raise on every show, above later override-redirect windows */
	};
	xcb_configure_window(resources->connection, resources->window, configure_mask, configure_values);

	if (resources->surface_size.width != window_size.width || resources->surface_size.height != window_size.height) {
		cairo_xcb_surface_set_size(resources->surface, window_size.width, window_size.height);
		resources->surface_size = window_size;
	}

	switch (enabled->display.kind) {
		case INDICATOR_DISPLAY_HIDDEN:
			xcb_map_window(resources->connection, resources->window);
			break;
		case INDICATOR_DISPLAY_VISIBLE:
			break;
	}

	return paint_banner(resources);
}

static cairo_status_t show_banner(indicator_enabled_t *enabled, chain_phase_t chain_phase, const char *banner_text)
{
	set_layout_text_safely(enabled->resources.layout, banner_text);
	const cairo_status_t status = place_and_paint_banner(enabled);
	if (status != CAIRO_STATUS_SUCCESS)
		return status;

	enabled->display.kind = INDICATOR_DISPLAY_VISIBLE;
	enabled->display.as.visible.chain_phase = chain_phase;
	snprintf(enabled->display.as.visible.text, sizeof(enabled->display.as.visible.text), "%s", banner_text);
	return CAIRO_STATUS_SUCCESS;
}

static void hide_banner(indicator_enabled_t *enabled)
{
	xcb_unmap_window(enabled->resources.connection, enabled->resources.window);
	xcb_flush(enabled->resources.connection);
	enabled->display.kind = INDICATOR_DISPLAY_HIDDEN;
}

/* Tears everything down. The cairo objects must be finished while the X
 * connection is still alive, hence the order, and the device is held by a
 * reference of our own so that the pointer is valid after the surface goes. */
static void release_resources(indicator_resources_t *resources)
{
	g_object_unref(resources->layout);
	pango_font_description_free(resources->font_description);
	cairo_destroy(resources->cairo_context);
	cairo_surface_finish(resources->surface);
	cairo_surface_destroy(resources->surface);
	cairo_device_finish(resources->device);
	cairo_device_destroy(resources->device);
	xcb_destroy_window(resources->connection, resources->window);
	xcb_flush(resources->connection);
}

/* One-way transition to the disabled state after a rendering error: sxhkd
 * keeps working, only the banner is gone. */
static void disable_after_error(cairo_status_t status)
{
	warn("Indicator disabled after a rendering error: %s.\n", cairo_status_to_string(status));
	release_resources(&indicator_singleton.as.enabled.resources);
	indicator_singleton.kind = INDICATOR_DISABLED;
}

void indicator_init(const indicator_settings_t *settings, xcb_connection_t *connection, xcb_screen_t *screen_of_window)
{
	switch (settings->kind) {
		case INDICATOR_SETTINGS_DISABLED:
			indicator_singleton.kind = INDICATOR_DISABLED;
			return;
		case INDICATOR_SETTINGS_ENABLED:
			break;
	}
	const indicator_config_t *config = &settings->as.enabled;

	xcb_visualtype_t *visual_type = find_visual_type(screen_of_window, screen_of_window->root_visual);
	if (visual_type == NULL)
		err("Indicator: can't find the root visual.\n");

	const xcb_window_t window = xcb_generate_id(connection);
	/* Values must follow the ascending bit order of their mask flags. An
	 * exposure-only event mask and override-redirect make the window
	 * invisible to the window manager and to keyboard focus. */
	const uint32_t attribute_mask = XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_SAVE_UNDER | XCB_CW_EVENT_MASK;
	const uint32_t attribute_values[] = {
		pixel_from_rgb_color(visual_type, screen_of_window, config->background_color),
		1,
		1,
		XCB_EVENT_MASK_EXPOSURE,
	};
	/* 1x1 because the protocol rejects zero-sized windows; resized on each show. */
	const xcb_void_cookie_t create_cookie = xcb_create_window_checked(connection, screen_of_window->root_depth, window, screen_of_window->root, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen_of_window->root_visual, attribute_mask, attribute_values);
	xcb_generic_error_t *create_error = xcb_request_check(connection, create_cookie);
	if (create_error != NULL) {
		const uint8_t error_code = create_error->error_code;
		free(create_error);
		err("Indicator: can't create the window (X error %u).\n", error_code);
	}
	make_window_transparent_to_input(connection, window);

	/* Follow screen size changes: the server sends ConfigureNotify for the
	 * root window to every client that selects StructureNotify on it, which
	 * includes resizes driven by RandR. sxhkd selects nothing else on root. */
	const uint32_t root_event_mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
	xcb_change_window_attributes(connection, screen_of_window->root, XCB_CW_EVENT_MASK, &root_event_mask);

	cairo_surface_t *surface = cairo_xcb_surface_create(connection, window, visual_type, 1, 1);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
		err("Indicator: can't create the cairo surface: %s.\n", cairo_status_to_string(cairo_surface_status(surface)));
	cairo_device_t *device = cairo_device_reference(cairo_surface_get_device(surface));
	cairo_t *cairo_context = cairo_create(surface);
	if (cairo_status(cairo_context) != CAIRO_STATUS_SUCCESS)
		err("Indicator: can't create the cairo context: %s.\n", cairo_status_to_string(cairo_status(cairo_context)));

	PangoLayout *layout = pango_cairo_create_layout(cairo_context);
	PangoFontDescription *font_description = pango_font_description_from_string(config->font_description_text);
	pango_layout_set_font_description(layout, font_description);
	pango_layout_set_single_paragraph_mode(layout, TRUE);
	const pixel_size_t screen_size = {screen_of_window->width_in_pixels, screen_of_window->height_in_pixels};
	pango_layout_set_width(layout, available_text_width_in_pango_units(screen_size));
	/* When the chain is too long for the screen, keep its most recent chords visible. */
	pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_START);

	/* Pay the one-time font loading cost now rather than on the first chain. */
	int warm_up_width = 0;
	int warm_up_height = 0;
	pango_layout_set_text(layout, "sxhkd", -1);
	pango_layout_get_pixel_size(layout, &warm_up_width, &warm_up_height);

	indicator_singleton.kind = INDICATOR_ENABLED;
	indicator_enabled_t *enabled = &indicator_singleton.as.enabled;
	enabled->resources.connection = connection;
	enabled->resources.root_window = screen_of_window->root;
	enabled->resources.window = window;
	enabled->resources.device = device;
	enabled->resources.surface = surface;
	enabled->resources.cairo_context = cairo_context;
	enabled->resources.layout = layout;
	enabled->resources.font_description = font_description;
	enabled->resources.config = *config;
	enabled->resources.screen_size = screen_size;
	enabled->resources.surface_size.width = 1;
	enabled->resources.surface_size.height = 1;
	enabled->display.kind = INDICATOR_DISPLAY_HIDDEN;
	xcb_flush(connection);
}

void indicator_sync_with_chain_phase(chain_phase_t chain_phase, const char *progress_text)
{
	switch (indicator_singleton.kind) {
		case INDICATOR_DISABLED:
			return;
		case INDICATOR_ENABLED:
			break;
	}
	indicator_enabled_t *enabled = &indicator_singleton.as.enabled;
	if (xcb_connection_has_error(enabled->resources.connection) != 0)
		return;

	static indicator_banner_t desired_banner;
	indicator_derive_banner(chain_phase, progress_text, &desired_banner);

	switch (desired_banner.kind) {
		case INDICATOR_BANNER_ABSENT:
			switch (enabled->display.kind) {
				case INDICATOR_DISPLAY_HIDDEN:
					break;
				case INDICATOR_DISPLAY_VISIBLE:
					hide_banner(enabled);
					break;
			}
			return;
		case INDICATOR_BANNER_PRESENT:
			switch (enabled->display.kind) {
				case INDICATOR_DISPLAY_HIDDEN:
					break;
				case INDICATOR_DISPLAY_VISIBLE:
					if (enabled->display.as.visible.chain_phase == chain_phase && strcmp(enabled->display.as.visible.text, desired_banner.text) == 0)
						return;
					break;
			}
			break;
	}

	const cairo_status_t show_status = show_banner(enabled, chain_phase, desired_banner.text);
	if (show_status != CAIRO_STATUS_SUCCESS)
		disable_after_error(show_status);
}

void indicator_handle_expose(const xcb_expose_event_t *expose_event)
{
	switch (indicator_singleton.kind) {
		case INDICATOR_DISABLED:
			return;
		case INDICATOR_ENABLED:
			break;
	}
	indicator_enabled_t *enabled = &indicator_singleton.as.enabled;
	if (expose_event->window != enabled->resources.window)
		return;
	/* A non-zero count announces more Expose events for the same window; one
	 * repaint after the last of them covers all of the uncovered area. */
	if (expose_event->count > 0)
		return;
	switch (enabled->display.kind) {
		case INDICATOR_DISPLAY_HIDDEN:
			return;
		case INDICATOR_DISPLAY_VISIBLE:
			break;
	}
	const cairo_status_t paint_status = paint_banner(&enabled->resources);
	if (paint_status != CAIRO_STATUS_SUCCESS)
		disable_after_error(paint_status);
}

void indicator_handle_configure_notify(const xcb_configure_notify_event_t *configure_event)
{
	switch (indicator_singleton.kind) {
		case INDICATOR_DISABLED:
			return;
		case INDICATOR_ENABLED:
			break;
	}
	indicator_enabled_t *enabled = &indicator_singleton.as.enabled;
	indicator_resources_t *resources = &enabled->resources;
	if (configure_event->window != resources->root_window)
		return;
	const pixel_size_t new_screen_size = {configure_event->width, configure_event->height};
	if (new_screen_size.width == resources->screen_size.width && new_screen_size.height == resources->screen_size.height)
		return;
	resources->screen_size = new_screen_size;
	pango_layout_set_width(resources->layout, available_text_width_in_pango_units(new_screen_size));
	switch (enabled->display.kind) {
		case INDICATOR_DISPLAY_HIDDEN:
			return;
		case INDICATOR_DISPLAY_VISIBLE:
			break;
	}
	/* The cached text is already in the layout; re-place and repaint it. */
	const cairo_status_t status = place_and_paint_banner(enabled);
	if (status != CAIRO_STATUS_SUCCESS)
		disable_after_error(status);
}

void indicator_shutdown(void)
{
	switch (indicator_singleton.kind) {
		case INDICATOR_DISABLED:
			return;
		case INDICATOR_ENABLED:
			break;
	}
	release_resources(&indicator_singleton.as.enabled.resources);
	indicator_singleton.kind = INDICATOR_DISABLED;
}
