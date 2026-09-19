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

#ifndef SXHKD_INDICATOR_H
#define SXHKD_INDICATOR_H

/* The on-screen chain indicator: a small override-redirect window that shows
 * the chords received so far while a chord chain is in progress.
 *
 * The module owns a private singleton. Every entry point is a no-op when the
 * indicator is disabled, and none of them may be called from a signal
 * handler or from a forked child. */

#include <xcb/xcb.h>
#include "indicator_core.h"

/* Creates the window and rendering resources (or nothing, if disabled).
 * Leaves the indicator disabled, with a warning, when the X server refuses
 * its window on every visual; exits the process through err() only when the
 * cairo side cannot be set up. Must run before the first call of any other
 * entry point. */
void indicator_init(const indicator_settings_t *settings, xcb_connection_t *connection, xcb_screen_t *screen_of_window, int screen_number);

/* Makes what is on screen match the given recorder state: shows, updates or
 * hides the banner as needed. Idempotent and cheap when nothing changed, so it
 * is called once per main-loop iteration. */
void indicator_sync_with_chain_phase(chain_phase_t chain_phase, const char *progress_text);

/* Repaints the banner from its cached text when the X server reports that
 * part of the window was uncovered. Ignores events for other windows. */
void indicator_handle_expose(const xcb_expose_event_t *expose_event);

/* Re-anchors (and, if visible, re-lays out) the banner after the root
 * window, i.e. the screen, changed size. Ignores other windows' events. */
void indicator_handle_configure_notify(const xcb_configure_notify_event_t *configure_event);

/* Releases every resource. Must run before xcb_disconnect(). */
void indicator_shutdown(void);

#endif
