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

#ifndef SXHKD_DIAGNOSTICS_H
#define SXHKD_DIAGNOSTICS_H

/* Diagnostics on stderr. This module refers to no daemon state, unlike
 * helpers.c, so that every binary (the daemon, the chain indicator, the unit
 * tests) can link it. This header deliberately includes nothing. */

/* Prints the formatted message on stderr. No newline is appended: the format
 * ends the message. */
__attribute__((format(printf, 1, 2)))
void warn(const char *format, ...);
/* Prints the formatted message on stderr and exits with EXIT_FAILURE through
 * exit(), so that the atexit handlers run. Never returns. */
__attribute__((noreturn, format(printf, 1, 2)))
void err(const char *format, ...);

#endif
