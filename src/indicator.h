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
 * Exits the process through err() if the user asked for an indicator that
 * cannot be set up. Must run before the first call of any other entry point. */
void indicator_init(const indicator_settings_t *settings, xcb_connection_t *connection, xcb_screen_t *screen_of_window);

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
