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
#include "indicator_options.h"

#define LENGTH(x) (sizeof(x) / sizeof(*x))

/* Options that take no value. */
typedef enum {
	FLAG_HELP,
	FLAG_VERSION
} flag_option_t;

/* Options that take a value. Their handler always receives one. */
typedef enum {
	VALUED_STATUS_FIFO,
	VALUED_POSITION,
	VALUED_FONT,
	VALUED_FOREGROUND,
	VALUED_BACKGROUND,
	VALUED_TIMEOUT
} valued_option_t;

/* The single source of truth for the command line. The short letters of the
 * look options are those of the daemon's --indicator-* options, so that a
 * command line moves from one program to the other unchanged. */
static const cli_option_spec_t option_specs[] = {
	CLI_FLAG('h', "help", FLAG_HELP, "Print this help and exit."),
	CLI_FLAG('v', "version", FLAG_VERSION, "Print the version and exit."),
	CLI_VALUED('s', "status-fifo", VALUED_STATUS_FIFO, "PATH",
		"Read the status lines of sxhkd -s PATH from the FIFO at PATH (required)."),
	CLI_VALUED('i', "position", VALUED_POSITION, "POSITION",
		"Anchor the banner at POSITION (see below; default top-right)."),
	CLI_VALUED('f', "font", VALUED_FONT, "FONT",
		"Banner font, a Pango description (default \"" INDICATOR_DEFAULT_FONT_DESCRIPTION "\")."),
	CLI_VALUED('F', "foreground", VALUED_FOREGROUND, "COLOR",
		"Text color, #rrggbb or #rrggbbaa (default " INDICATOR_DEFAULT_FOREGROUND_COLOR ")."),
	CLI_VALUED('B', "background", VALUED_BACKGROUND, "COLOR",
		"Background color, #rrggbb or #rrggbbaa (default " INDICATOR_DEFAULT_BACKGROUND_COLOR ")."),
	CLI_VALUED('t', "timeout", VALUED_TIMEOUT, "SECONDS",
		"Hide a chain in progress after SECONDS without a line; 0: never (default 3)."),
};

static void append_position_names(char *buffer, size_t capacity)
{
	for (size_t index = 0; index < indicator_position_name_count; index++) {
		const char *separator = (index == 0) ? "" : (index + 1 == indicator_position_name_count) ? " or " : ", ";
		const size_t used = strlen(buffer);
		if (used >= capacity)
			return;
		snprintf(buffer + used, capacity - used, "%s%s", separator, indicator_position_names[index].name);
	}
}

/* Every rejected value is reported the same way; `expectation` completes
 * "expected ...". */
static cli_apply_result_t invalid_value(cli_error_t *error, const char *value, const char *option_as_written, const char *expectation)
{
	return cli_apply_invalid(error, "invalid value '%s' for %s: expected %s", value, option_as_written, expectation);
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

/* The parser state is the run options themselves: every option of this
 * program has a default, except the FIFO path, whose absence the NULL
 * pointer records until the end of the parse. */
static cli_apply_result_t apply_valued(int valued_id, const char *option_as_written, const char *value, void *state, cli_error_t *error)
{
	indicator_run_options_t *run_options = state;
	cli_apply_result_t result = CLI_APPLY_CONTINUE;
	switch ((valued_option_t) valued_id) {
		case VALUED_STATUS_FIFO:
			run_options->status_fifo_path = value;
			break;
		case VALUED_POSITION:
			if (!indicator_parse_position(value, &run_options->config.position)) {
				char position_names[MAXLEN] = "";
				append_position_names(position_names, sizeof(position_names));
				result = invalid_value(error, value, option_as_written, position_names);
			}
			break;
		case VALUED_FONT:
			run_options->config.font_description_text = value;
			break;
		case VALUED_FOREGROUND:
			if (!indicator_parse_rgba_color(value, &run_options->config.foreground_color))
				result = invalid_value(error, value, option_as_written, "a color as #rrggbb or #rrggbbaa");
			break;
		case VALUED_BACKGROUND:
			if (!indicator_parse_rgba_color(value, &run_options->config.background_color))
				result = invalid_value(error, value, option_as_written, "a color as #rrggbb or #rrggbbaa");
			break;
		case VALUED_TIMEOUT:
			if (!cli_parse_integer(value, 0, INT_MAX, &run_options->timeout_in_seconds))
				result = invalid_value(error, value, option_as_written, "a non-negative integer");
			break;
	}
	return result;
}

/* False iff a built-in default is broken: a programming error, not user
 * input, reported through `error`. */
static bool initialize_run_options(indicator_run_options_t *run_options, cli_error_t *error)
{
	run_options->status_fifo_path = NULL;
	run_options->config.position = INDICATOR_POSITION_TOP_RIGHT;
	run_options->config.font_description_text = INDICATOR_DEFAULT_FONT_DESCRIPTION;
	run_options->timeout_in_seconds = INDICATOR_DEFAULT_TIMEOUT_IN_SECONDS;
	/* The built-in colors are constants verified by the unit test of the core. */
	if (!indicator_parse_rgba_color(INDICATOR_DEFAULT_FOREGROUND_COLOR, &run_options->config.foreground_color)
			|| !indicator_parse_rgba_color(INDICATOR_DEFAULT_BACKGROUND_COLOR, &run_options->config.background_color)) {
		cli_apply_invalid(error, "the built-in indicator colors are invalid");
		return false;
	}
	return true;
}

indicator_command_line_t parse_indicator_command_line(int argument_count, char **arguments)
{
	indicator_command_line_t command_line;
	indicator_run_options_t run_options;
	if (!initialize_run_options(&run_options, &command_line.as.invalid)) {
		command_line.kind = INDICATOR_COMMAND_LINE_INVALID;
		return command_line;
	}

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
			if (outcome.as.run.positional_count > 0) {
				command_line.kind = INDICATOR_COMMAND_LINE_INVALID;
				cli_apply_invalid(&command_line.as.invalid, "unexpected argument '%s'", outcome.as.run.positionals[0]);
				break;
			}
			if (run_options.status_fifo_path == NULL) {
				command_line.kind = INDICATOR_COMMAND_LINE_INVALID;
				cli_apply_invalid(&command_line.as.invalid, "the status FIFO path is required (-s PATH)");
				break;
			}
			command_line.kind = INDICATOR_COMMAND_LINE_RUN;
			command_line.as.run = run_options;
			break;
		case CLI_OUTCOME_SHOW_HELP:
			command_line.kind = INDICATOR_COMMAND_LINE_SHOW_HELP;
			break;
		case CLI_OUTCOME_SHOW_VERSION:
			command_line.kind = INDICATOR_COMMAND_LINE_SHOW_VERSION;
			break;
		case CLI_OUTCOME_INVALID:
			command_line.kind = INDICATOR_COMMAND_LINE_INVALID;
			command_line.as.invalid = outcome.as.invalid;
			break;
	}
	return command_line;
}

void print_indicator_usage(FILE *output)
{
	char position_names[MAXLEN] = "";
	append_position_names(position_names, sizeof(position_names));
	char notes[2 * MAXLEN];
	snprintf(notes, sizeof(notes), "Positions for --position:\n  %s.\n", position_names);

	const cli_help_t help = {
		.program_name = "sxhkd-indicator",
		.usage_arguments = "-s PATH [OPTION]...",
		.summary = "On-screen chord-chain indicator for sxhkd: shows the chain in progress read from its status FIFO.",
		.specs = option_specs,
		.spec_count = LENGTH(option_specs),
		.notes = notes,
		.epilogue = "See sxhkd-indicator(1), and the status FIFO protocol in sxhkd(1).\n",
	};
	cli_print_help(output, &help);
}
