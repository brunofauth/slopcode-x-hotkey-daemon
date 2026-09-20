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
	VALUED_ABORT_KEYSYM,
	VALUED_INDICATOR,
	VALUED_INDICATOR_FONT,
	VALUED_INDICATOR_FOREGROUND,
	VALUED_INDICATOR_BACKGROUND
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
	CLI_VALUED('i', "indicator", VALUED_INDICATOR, "POSITION",
		"Show the chain indicator at POSITION (see below)."),
	CLI_VALUED('f', "indicator-font", VALUED_INDICATOR_FONT, "FONT",
		"Indicator font, a Pango description (default \"" INDICATOR_DEFAULT_FONT_DESCRIPTION "\")."),
	CLI_VALUED('F', "indicator-foreground", VALUED_INDICATOR_FOREGROUND, "COLOR",
		"Indicator text color, #rrggbb or #rrggbbaa (default " INDICATOR_DEFAULT_FOREGROUND_COLOR ")."),
	CLI_VALUED('B', "indicator-background", VALUED_INDICATOR_BACKGROUND, "COLOR",
		"Indicator background color, #rrggbb or #rrggbbaa (default " INDICATOR_DEFAULT_BACKGROUND_COLOR ")."),
};

/* Parser state beyond the run options: the indicator configuration is
 * assembled here and only becomes part of the result once --indicator is
 * known, so the result never holds a "disabled" indicator carrying half a
 * configuration. */
typedef struct {
	run_options_t run_options;
	indicator_config_t indicator_config;
	bool indicator_position_given;
	bool indicator_look_given;
} parser_state_t;

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
	parser_state_t *parser_state = state;
	run_options_t *run_options = &parser_state->run_options;
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
		case VALUED_INDICATOR:
			if (!indicator_parse_position(value, &parser_state->indicator_config.position)) {
				char position_names[MAXLEN] = "";
				append_position_names(position_names, sizeof(position_names));
				result = invalid_value(error, value, option_as_written, position_names);
				break;
			}
			parser_state->indicator_position_given = true;
			break;
		case VALUED_INDICATOR_FONT:
			parser_state->indicator_config.font_description_text = value;
			parser_state->indicator_look_given = true;
			break;
		case VALUED_INDICATOR_FOREGROUND:
			if (!indicator_parse_rgba_color(value, &parser_state->indicator_config.foreground_color)) {
				result = invalid_value(error, value, option_as_written, "a color as #rrggbb or #rrggbbaa");
				break;
			}
			parser_state->indicator_look_given = true;
			break;
		case VALUED_INDICATOR_BACKGROUND:
			if (!indicator_parse_rgba_color(value, &parser_state->indicator_config.background_color)) {
				result = invalid_value(error, value, option_as_written, "a color as #rrggbb or #rrggbbaa");
				break;
			}
			parser_state->indicator_look_given = true;
			break;
	}
	return result;
}

/* False iff a built-in default is broken: a programming error, not user
 * input, reported through `error`. */
static bool initialize_parser_state(parser_state_t *state, cli_error_t *error)
{
	run_options_t *run_options = &state->run_options;
	run_options->mapping_count = DEFAULT_MAPPING_COUNT;
	run_options->timeout_in_seconds = DEFAULT_CHAIN_TIMEOUT_IN_SECONDS;
	run_options->config_path = NULL;
	run_options->redirect_path = NULL;
	run_options->status_fifo_count = 0;
	run_options->abort_keysym_name = NULL;
	run_options->indicator.kind = INDICATOR_SETTINGS_DISABLED;
	run_options->indicator_look_given_without_position = false;
	run_options->extra_config_count = 0;
	run_options->extra_config_paths = NULL;

	state->indicator_config.position = INDICATOR_POSITION_TOP_RIGHT;
	state->indicator_config.font_description_text = INDICATOR_DEFAULT_FONT_DESCRIPTION;
	state->indicator_position_given = false;
	state->indicator_look_given = false;
	/* The built-in colors are constants verified by the unit test. */
	if (!indicator_parse_rgba_color(INDICATOR_DEFAULT_FOREGROUND_COLOR, &state->indicator_config.foreground_color)
			|| !indicator_parse_rgba_color(INDICATOR_DEFAULT_BACKGROUND_COLOR, &state->indicator_config.background_color)) {
		cli_apply_invalid(error, "the built-in indicator colors are invalid");
		return false;
	}
	return true;
}

/* The indicator is enabled iff a position was given; a look given without
 * one is only flagged, so that main can warn. */
static run_options_t finalize_run_options(const parser_state_t *state, int extra_config_count, char **extra_config_paths)
{
	run_options_t run_options = state->run_options;
	if (state->indicator_position_given) {
		run_options.indicator.kind = INDICATOR_SETTINGS_ENABLED;
		run_options.indicator.as.enabled = state->indicator_config;
	} else {
		run_options.indicator.kind = INDICATOR_SETTINGS_DISABLED;
	}
	run_options.indicator_look_given_without_position = state->indicator_look_given && !state->indicator_position_given;
	run_options.extra_config_count = extra_config_count;
	run_options.extra_config_paths = extra_config_paths;
	return run_options;
}

command_line_t parse_command_line(int argument_count, char **arguments)
{
	command_line_t command_line;
	parser_state_t state;
	if (!initialize_parser_state(&state, &command_line.as.invalid)) {
		command_line.kind = COMMAND_LINE_INVALID;
		return command_line;
	}

	const cli_table_t table = {
		.specs = option_specs,
		.spec_count = LENGTH(option_specs),
		.state = &state,
		.apply_flag = apply_flag,
		.apply_valued = apply_valued,
	};
	const cli_outcome_t outcome = cli_parse(&table, argument_count, arguments);
	switch (outcome.kind) {
		case CLI_OUTCOME_RUN:
			command_line.kind = COMMAND_LINE_RUN;
			command_line.as.run = finalize_run_options(&state, outcome.as.run.positional_count, outcome.as.run.positionals);
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
			break;
	}
	return command_line;
}

void print_usage(FILE *output)
{
	char position_names[MAXLEN] = "";
	append_position_names(position_names, sizeof(position_names));
	char notes[2 * MAXLEN];
	snprintf(notes, sizeof(notes), "Positions for --indicator:\n  %s.\n", position_names);

	const cli_help_t help = {
		.program_name = "sxhkd",
		.usage_arguments = "[OPTION]... [EXTRA_CONFIG]...",
		.summary = "Simple X hotkey daemon: runs commands on key chords and chord chains.",
		.specs = option_specs,
		.spec_count = LENGTH(option_specs),
		.notes = notes,
		.epilogue = "See sxhkd(1) for the configuration syntax and the status FIFO protocol.\n",
	};
	cli_print_help(output, &help);
}
