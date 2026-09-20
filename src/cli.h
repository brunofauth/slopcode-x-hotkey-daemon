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

#ifndef SXHKD_CLI_H
#define SXHKD_CLI_H

/* The table-driven command-line engine shared by every program of this
 * package (the daemon, the chain indicator). One table of option
 * specifications drives the argv walk, the generated --help text and the
 * diagnostics, so they cannot disagree. The engine knows nothing about what
 * an option means: the program applies each one through the callbacks of its
 * table, into its own typed state, and reports failures back through the
 * same channel as the engine's own diagnostics. Pure C99 and libc:
 * unit-tested headlessly (test/cli_test.c).
 *
 * Accepted forms: --name VALUE, --name=VALUE, -x VALUE, -xVALUE, clusters of
 * short flags (-ab; the first valued option of a cluster takes the rest of it
 * as its value), and -- to end the options. Arguments are permuted: the
 * non-option arguments are compacted, in order, to arguments[1..]. */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "chain_phase.h"

/* The capacity of the message of an invalid command line: room for a value
 * quoted in full plus the option and what was expected. */
#define CLI_MESSAGE_CAPACITY (2 * MAXLEN)

typedef enum {
	CLI_OPTION_FLAG,     /* takes no value */
	CLI_OPTION_VALUED    /* takes a value; its callback always receives one */
} cli_option_kind_t;

/* One option. The ids are the program's own enumerators, stored as int so
 * that one engine serves every program: the callbacks of cli_table_t cast
 * them back to the enumeration the table was built from and switch on it
 * exhaustively. A flag and a valued option live in separate enumerations,
 * as they reach separate callbacks. The fields are ordered by size so that
 * a table row carries no needless padding. */
typedef struct {
	char short_name;
	cli_option_kind_t kind;
	const char *long_name;
	const char *description;   /* one short line: the help text puts it in a column */
	union {
		struct {
			int id;
		} flag;                              /* valid iff kind == CLI_OPTION_FLAG */
		struct {
			int id;
			const char *argument_name;       /* e.g. "FILE": shown in the help and the diagnostics */
		} valued;                            /* valid iff kind == CLI_OPTION_VALUED */
	} as;
} cli_option_spec_t;

/* Initializers for the rows of a table. (The parameters are named apart from
 * the fields: a macro parameter is substituted after the '.' of a designator
 * as anywhere else.) */
#define CLI_FLAG(short_letter, long_word, option_id, help_line) \
	{.short_name = short_letter, .kind = CLI_OPTION_FLAG, .long_name = long_word, .description = help_line, \
	 .as = {.flag = {.id = option_id}}}
#define CLI_VALUED(short_letter, long_word, option_id, argument_label, help_line) \
	{.short_name = short_letter, .kind = CLI_OPTION_VALUED, .long_name = long_word, .description = help_line, \
	 .as = {.valued = {.id = option_id, .argument_name = argument_label}}}

/* The message of an invalid command line, without a trailing newline. */
typedef struct {
	char message[CLI_MESSAGE_CAPACITY];
} cli_error_t;

/* What applying one option decides for the rest of the parse. */
typedef enum {
	CLI_APPLY_CONTINUE,       /* applied: keep parsing */
	CLI_APPLY_SHOW_HELP,      /* stop: the program prints its help */
	CLI_APPLY_SHOW_VERSION,   /* stop: the program prints its version */
	CLI_APPLY_INVALID         /* stop: the message was written into the error */
} cli_apply_result_t;

/* Formats the message of the error and returns CLI_APPLY_INVALID, so that a
 * callback can `return cli_apply_invalid(error, ...)`. */
__attribute__((format(printf, 2, 3)))
cli_apply_result_t cli_apply_invalid(cli_error_t *error, const char *format, ...);

/* The table of one program: its options and how to apply them. In both
 * callbacks, `option_as_written` is "-x" or "--long-name" for diagnostics
 * and `state` is the program's own parser state, passed through untouched.
 * On CLI_APPLY_INVALID the callback has filled `error`. */
typedef struct {
	const cli_option_spec_t *specs;
	size_t spec_count;
	void *state;
	cli_apply_result_t (*apply_flag)(int flag_id, const char *option_as_written, void *state, cli_error_t *error);
	/* `value` is never empty: the engine rejects an empty value for every
	 * valued option before the callback sees it. */
	cli_apply_result_t (*apply_valued)(int valued_id, const char *option_as_written, const char *value, void *state, cli_error_t *error);
} cli_table_t;

typedef enum {
	CLI_OUTCOME_RUN,
	CLI_OUTCOME_SHOW_HELP,
	CLI_OUTCOME_SHOW_VERSION,
	CLI_OUTCOME_INVALID
} cli_outcome_kind_t;

typedef struct {
	cli_outcome_kind_t kind;
	union {
		struct {
			int positional_count;
			char **positionals;   /* arguments[1..], in order */
		} run;                    /* valid iff kind == CLI_OUTCOME_RUN */
		cli_error_t invalid;      /* valid iff kind == CLI_OUTCOME_INVALID */
	} as;
} cli_outcome_t;

/* Parses argv-style arguments against the table. Non-option arguments are
 * moved, in order, to arguments[1..] (the strings themselves are never
 * modified); every pointer in the outcome points into those strings and
 * stays valid for their lifetime. Parsing stops at the first option whose
 * callback does not continue, or at the first diagnostic. */
cli_outcome_t cli_parse(const cli_table_t *table, int argument_count, char **arguments);

/* The help text of one program, generated from its table:
 *
 *   Usage: PROGRAM_NAME USAGE_ARGUMENTS
 *   SUMMARY
 *
 *   Options:
 *     -x, --long-name ARG  DESCRIPTION      (one row per spec, aligned)
 *
 *   NOTES                                    (verbatim, if given)
 *   Long options also accept --name=VALUE; -- ends the options.
 *   EPILOGUE                                 (verbatim, if given)
 *
 * The two verbatim texts must end their last line themselves. */
typedef struct {
	const char *program_name;
	const char *usage_arguments;   /* what follows the program name on the usage line */
	const char *summary;           /* one line */
	const cli_option_spec_t *specs;
	size_t spec_count;
	const char *notes;             /* NULL: nothing */
	const char *epilogue;          /* NULL: nothing */
} cli_help_t;

void cli_print_help(FILE *output, const cli_help_t *help);

/* Accepts exactly an optional '-' followed by decimal digits, within
 * [minimum, maximum]. Stricter than strtol, which also takes leading
 * whitespace and a '+' sign. Never writes on failure. */
bool cli_parse_integer(const char *text, int minimum, int maximum, int *parsed_value);

#endif
