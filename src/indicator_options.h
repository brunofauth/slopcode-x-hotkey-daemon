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

#ifndef SXHKD_INDICATOR_OPTIONS_H
#define SXHKD_INDICATOR_OPTIONS_H

/* The chain indicator's command line: its option table and the typed
 * result. The argv walk, the diagnostics and the layout of the help text
 * come from the engine in cli.h. Pure C99 and libc. */

#include <stdio.h>
#include "cli.h"
#include "indicator_core.h"

#define INDICATOR_DEFAULT_TIMEOUT_IN_SECONDS 3

typedef struct {
	const char *status_fifo_path;   /* never NULL: the option is required */
	indicator_config_t config;      /* always enabled: this program is the indicator */
	int timeout_in_seconds;         /* 0: never hide a chain in progress on its own */
} indicator_run_options_t;

typedef enum {
	INDICATOR_COMMAND_LINE_RUN,
	INDICATOR_COMMAND_LINE_SHOW_HELP,
	INDICATOR_COMMAND_LINE_SHOW_VERSION,
	INDICATOR_COMMAND_LINE_INVALID
} indicator_command_line_kind_t;

typedef struct {
	indicator_command_line_kind_t kind;
	union {
		indicator_run_options_t run;   /* valid iff kind == INDICATOR_COMMAND_LINE_RUN */
		cli_error_t invalid;           /* valid iff kind == INDICATOR_COMMAND_LINE_INVALID; no trailing newline */
	} as;
} indicator_command_line_t;

/* Parses argv-style arguments. The program takes no non-option argument:
 * one is a diagnostic, as is a run without --status-fifo. Every pointer in
 * the result points into the argument strings (which are never modified)
 * and stays valid for their lifetime. */
indicator_command_line_t parse_indicator_command_line(int argument_count, char **arguments);

/* Prints the help text generated from the option table. */
void print_indicator_usage(FILE *output);

#endif
