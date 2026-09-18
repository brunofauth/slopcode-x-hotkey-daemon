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
