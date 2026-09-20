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

#include <limits.h>
#include <string.h>
#include "options.h"

/* Options that take no value. */
typedef enum {
	FLAG_HELP,
	FLAG_VERSION
} flag_option_t;

/* Options that take a value. Their handler always receives one. */
typedef enum {
	VALUED_MAPPING_COUNT,
	VALUED_TIMEOUT,
	VALUED_CONFIG,
	VALUED_REDIRECT,
	VALUED_STATUS_FIFO,
	VALUED_ABORT_KEYSYM
} valued_option_t;

/* The single source of truth for the command line. Keep descriptions short:
 * the help text puts them in a column to the right of the option names. */
static const cli_option_spec_t option_specs[] = {
	CLI_FLAG('h', "help", FLAG_HELP, "Print this help and exit."),
	CLI_FLAG('v', "version", FLAG_VERSION, "Print the version and exit."),
	CLI_VALUED('m', "mapping-count", VALUED_MAPPING_COUNT, "COUNT",
		"Handle the first COUNT (>= 0) mapping notify events; -1: all (default 0)."),
	CLI_VALUED('t', "timeout", VALUED_TIMEOUT, "SECONDS",
		"Abort a chord chain after SECONDS without a chord; 0: never (default 3)."),
	CLI_VALUED('c', "config", VALUED_CONFIG, "FILE",
		"Read the main configuration from FILE."),
	CLI_VALUED('r', "redirect", VALUED_REDIRECT, "FILE",
		"Redirect the output of the commands to FILE."),
	CLI_VALUED('s', "status-fifo", VALUED_STATUS_FIFO, "PATH",
		"Report status lines to the FIFO at PATH, created if absent (repeatable)."),
	CLI_VALUED('a', "abort-keysym", VALUED_ABORT_KEYSYM, "KEYSYM",
		"Keysym that aborts a chord chain (default Escape)."),
};

/* The options of the chain indicator that used to live inside the daemon,
 * gone since the indicator became a program of its own (sxhkd-indicator).
 * They are deliberately not rows of the table: a row would be listed by the
 * help and would take a value like a live option. The engine reports them as
 * unknown, and the daemon appends where they went (see
 * explain_removed_indicator_option()), so that a command line written for an
 * older version explains its own failure. */
static const char *const removed_indicator_long_names[] = {
	"indicator", "indicator-font", "indicator-foreground", "indicator-background"
};
static const char removed_indicator_short_names[] = "ifFB";
#define REMOVED_INDICATOR_HINT "the chain indicator is now sxhkd-indicator(1)"

/* Every rejected value is reported the same way; `expectation` completes
 * "expected ...". */
static cli_apply_result_t invalid_value(cli_error_t *error, const char *value, const char *option_as_written, const char *expectation)
{
	return cli_apply_invalid(error, "invalid value '%s' for %s: expected %s", value, option_as_written, expectation);
}

/* Each -s adds a FIFO; the same path twice would receive every line twice,
 * so it is a mistake rather than a no-op. */
static cli_apply_result_t add_status_fifo_path(run_options_t *run_options, const char *value, const char *option_as_written, cli_error_t *error)
{
	for (int index = 0; index < run_options->status_fifo_count; index++) {
		if (strcmp(run_options->status_fifo_paths[index], value) == 0)
			return cli_apply_invalid(error, "invalid value '%s' for %s: given twice", value, option_as_written);
	}
	if (run_options->status_fifo_count == MAX_STATUS_FIFOS)
		return cli_apply_invalid(error, "too many status FIFOs (at most %d)", MAX_STATUS_FIFOS);
	run_options->status_fifo_paths[run_options->status_fifo_count] = value;
	run_options->status_fifo_count++;
	return CLI_APPLY_CONTINUE;
}

static cli_apply_result_t apply_flag(int flag_id, const char *option_as_written, void *state, cli_error_t *error)
{
	(void) option_as_written;
	(void) state;
	(void) error;
	cli_apply_result_t result = CLI_APPLY_CONTINUE;
	switch ((flag_option_t) flag_id) {
		case FLAG_HELP:
			result = CLI_APPLY_SHOW_HELP;
			break;
		case FLAG_VERSION:
			result = CLI_APPLY_SHOW_VERSION;
			break;
	}
	return result;
}

static cli_apply_result_t apply_valued(int valued_id, const char *option_as_written, const char *value, void *state, cli_error_t *error)
{
	run_options_t *run_options = state;
	cli_apply_result_t result = CLI_APPLY_CONTINUE;
	switch ((valued_option_t) valued_id) {
		case VALUED_MAPPING_COUNT:
			if (!cli_parse_integer(value, -1, INT_MAX, &run_options->mapping_count))
				result = invalid_value(error, value, option_as_written, "-1 or a non-negative integer");
			break;
		case VALUED_TIMEOUT:
			if (!cli_parse_integer(value, 0, INT_MAX, &run_options->timeout_in_seconds))
				result = invalid_value(error, value, option_as_written, "a non-negative integer");
			break;
		case VALUED_CONFIG:
			run_options->config_path = value;
			break;
		case VALUED_REDIRECT:
			run_options->redirect_path = value;
			break;
		case VALUED_STATUS_FIFO:
			result = add_status_fifo_path(run_options, value, option_as_written, error);
			break;
		case VALUED_ABORT_KEYSYM:
			run_options->abort_keysym_name = value;
			break;
	}
	return result;
}

static void initialize_run_options(run_options_t *run_options)
{
	run_options->mapping_count = DEFAULT_MAPPING_COUNT;
	run_options->timeout_in_seconds = DEFAULT_CHAIN_TIMEOUT_IN_SECONDS;
	run_options->config_path = NULL;
	run_options->redirect_path = NULL;
	run_options->status_fifo_count = 0;
	run_options->abort_keysym_name = NULL;
	run_options->extra_config_count = 0;
	run_options->extra_config_paths = NULL;
}

/* True iff `message` is the engine's diagnostic for an unknown option that
 * used to be one of the indicator's: "unrecognized option '--NAME'" (or
 * '--NAME=VALUE') for a long one, "invalid option -- 'X'" for a short one.
 * The name is matched whole, so a mere near-miss gets no hint. */
static bool names_removed_indicator_option(const char *message)
{
	static const char long_prefix[] = "unrecognized option '--";
	static const char short_prefix[] = "invalid option -- '";
	if (strncmp(message, long_prefix, sizeof(long_prefix) - 1) == 0) {
		const char *name = message + sizeof(long_prefix) - 1;
		const size_t name_length = strcspn(name, "='");
		for (size_t index = 0; index < LENGTH(removed_indicator_long_names); index++) {
			const char *removed_name = removed_indicator_long_names[index];
			if (strlen(removed_name) == name_length && strncmp(name, removed_name, name_length) == 0)
				return true;
		}
		return false;
	}
	if (strncmp(message, short_prefix, sizeof(short_prefix) - 1) == 0) {
		const char *letter = message + sizeof(short_prefix) - 1;
		return letter[0] != '\0' && letter[1] == '\'' && strchr(removed_indicator_short_names, letter[0]) != NULL;
	}
	return false;
}

/* Appends the hint to the engine's diagnostic, which stays as written. */
static void explain_removed_indicator_option(cli_error_t *error)
{
	char engine_message[CLI_MESSAGE_CAPACITY];
	snprintf(engine_message, sizeof(engine_message), "%s", error->message);
	cli_apply_invalid(error, "%s: " REMOVED_INDICATOR_HINT, engine_message);
}

command_line_t parse_command_line(int argument_count, char **arguments)
{
	command_line_t command_line;
	run_options_t run_options;
	initialize_run_options(&run_options);

	const cli_table_t table = {
		.specs = option_specs,
		.spec_count = LENGTH(option_specs),
		.state = &run_options,
		.apply_flag = apply_flag,
		.apply_valued = apply_valued,
	};
	const cli_outcome_t outcome = cli_parse(&table, argument_count, arguments);
	switch (outcome.kind) {
		case CLI_OUTCOME_RUN:
			command_line.kind = COMMAND_LINE_RUN;
			command_line.as.run = run_options;
			command_line.as.run.extra_config_count = outcome.as.run.positional_count;
			command_line.as.run.extra_config_paths = outcome.as.run.positionals;
			break;
		case CLI_OUTCOME_SHOW_HELP:
			command_line.kind = COMMAND_LINE_SHOW_HELP;
			break;
		case CLI_OUTCOME_SHOW_VERSION:
			command_line.kind = COMMAND_LINE_SHOW_VERSION;
			break;
		case CLI_OUTCOME_INVALID:
			command_line.kind = COMMAND_LINE_INVALID;
			command_line.as.invalid = outcome.as.invalid;
			if (names_removed_indicator_option(command_line.as.invalid.message))
				explain_removed_indicator_option(&command_line.as.invalid);
			break;
	}
	return command_line;
}

void print_usage(FILE *output)
{
	const cli_help_t help = {
		.program_name = "sxhkd",
		.usage_arguments = "[OPTION]... [EXTRA_CONFIG]...",
		.summary = "Simple X hotkey daemon: runs commands on key chords and chord chains.",
		.specs = option_specs,
		.spec_count = LENGTH(option_specs),
		.notes = NULL,
		.epilogue = "See sxhkd(1) for the configuration syntax and the status FIFO protocol.\n"
		            "The on-screen chain indicator is a program of its own: see sxhkd-indicator(1).\n",
	};
	cli_print_help(output, &help);
}
