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

#ifndef SXHKD_OPTIONS_H
#define SXHKD_OPTIONS_H

/* Command-line parsing. One table of option specifications drives the
 * parser, the generated --help text and the diagnostics, so they cannot
 * disagree. Pure C99 and libc: unit-tested headlessly (test/options_test.c). */

#include <stdbool.h>
#include <stdio.h>
#include "helpers.h"
#include "indicator_core.h"

#define DEFAULT_CHAIN_TIMEOUT_IN_SECONDS 3
#define DEFAULT_MAPPING_COUNT            0

typedef struct {
	int mapping_count;
	int timeout_in_seconds;
	const char *config_path;          /* NULL: the default location */
	const char *redirect_path;        /* NULL: no redirection */
	const char *status_fifo_path;     /* NULL: no status FIFO */
	const char *abort_keysym_name;    /* NULL: the default keysym */
	indicator_settings_t indicator;   /* disabled unless --indicator was given */
	/* --indicator-font/-foreground/-background were given but --indicator was not. */
	bool indicator_look_given_without_position;
	int extra_config_count;
	char **extra_config_paths;        /* the non-option arguments, in order */
} run_options_t;

typedef enum {
	COMMAND_LINE_RUN,
	COMMAND_LINE_SHOW_HELP,
	COMMAND_LINE_SHOW_VERSION,
	COMMAND_LINE_INVALID
} command_line_kind_t;

typedef struct {
	command_line_kind_t kind;
	union {
		run_options_t run;                         /* valid iff kind == COMMAND_LINE_RUN */
		struct { char message[2 * MAXLEN]; } invalid;   /* valid iff kind == COMMAND_LINE_INVALID; no trailing newline */
	} as;
} command_line_t;

/* Parses argv-style arguments. Non-option arguments are moved, in order, to
 * arguments[1..] (the strings themselves are never modified); every pointer in
 * the result points into those strings and stays valid for their lifetime. */
command_line_t parse_command_line(int argument_count, char **arguments);

/* Prints the help text generated from the option table. */
void print_usage(FILE *output);

#endif
