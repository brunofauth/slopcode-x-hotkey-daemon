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

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
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

typedef enum {
	OPTION_KIND_FLAG,
	OPTION_KIND_VALUED
} option_kind_t;

typedef struct {
	char short_name;
	const char *long_name;
	const char *description;
	option_kind_t kind;
	union {
		flag_option_t flag;                 /* valid iff kind == OPTION_KIND_FLAG */
		struct {
			valued_option_t id;
			const char *argument_name;
		} valued;                           /* valid iff kind == OPTION_KIND_VALUED */
	} as;
} option_spec_t;

#define FLAG(short_name, long_name, id, description) \
	{short_name, long_name, description, OPTION_KIND_FLAG, {.flag = id}}
#define VALUED(short_name, long_name, id, argument_name, description) \
	{short_name, long_name, description, OPTION_KIND_VALUED, {.valued = {id, argument_name}}}

/* The single source of truth for the command line. Keep descriptions short:
 * the help text puts them in a column to the right of the option names. */
static const option_spec_t option_specs[] = {
	FLAG('h', "help", FLAG_HELP, "Print this help and exit."),
	FLAG('v', "version", FLAG_VERSION, "Print the version and exit."),
	VALUED('m', "mapping-count", VALUED_MAPPING_COUNT, "COUNT",
		"Handle the first COUNT mapping notify events; -1: all (default 0)."),
	VALUED('t', "timeout", VALUED_TIMEOUT, "SECONDS",
		"Abort a chord chain after SECONDS without a chord; 0: never (default 3)."),
	VALUED('c', "config", VALUED_CONFIG, "FILE",
		"Read the main configuration from FILE."),
	VALUED('r', "redirect", VALUED_REDIRECT, "FILE",
		"Redirect the output of the commands to FILE."),
	VALUED('s', "status-fifo", VALUED_STATUS_FIFO, "PATH",
		"Report status lines to the FIFO at PATH, created if absent."),
	VALUED('a', "abort-keysym", VALUED_ABORT_KEYSYM, "KEYSYM",
		"Keysym that aborts a chord chain (default Escape)."),
	VALUED('i', "indicator", VALUED_INDICATOR, "POSITION",
		"Show the chain indicator at POSITION (see below)."),
	VALUED('f', "indicator-font", VALUED_INDICATOR_FONT, "FONT",
		"Indicator font, a Pango description (default \"" INDICATOR_DEFAULT_FONT_DESCRIPTION "\")."),
	VALUED('F', "indicator-foreground", VALUED_INDICATOR_FOREGROUND, "COLOR",
		"Indicator text color, #rrggbb or #rrggbbaa (default " INDICATOR_DEFAULT_FOREGROUND_COLOR ")."),
	VALUED('B', "indicator-background", VALUED_INDICATOR_BACKGROUND, "COLOR",
		"Indicator background color, #rrggbb or #rrggbbaa (default " INDICATOR_DEFAULT_BACKGROUND_COLOR ")."),
};

/* Parser state beyond the result: the indicator configuration is assembled
 * here and only becomes part of the result once --indicator is known, so the
 * result never holds a "disabled" indicator carrying half a configuration. */
typedef struct {
	command_line_t result;
	indicator_config_t indicator_config;
	bool indicator_position_given;
	bool indicator_look_given;
} parser_state_t;

static const option_spec_t *find_spec_by_short_name(char short_name)
{
	for (size_t index = 0; index < LENGTH(option_specs); index++) {
		if (option_specs[index].short_name == short_name)
			return &option_specs[index];
	}
	return NULL;
}

static const option_spec_t *find_spec_by_long_name(const char *long_name, size_t long_name_length)
{
	for (size_t index = 0; index < LENGTH(option_specs); index++) {
		const char *candidate = option_specs[index].long_name;
		if (strlen(candidate) == long_name_length && strncmp(candidate, long_name, long_name_length) == 0)
			return &option_specs[index];
	}
	return NULL;
}

__attribute__((format(printf, 2, 3)))
static void set_invalid(command_line_t *result, const char *format, ...)
{
	result->kind = COMMAND_LINE_INVALID;
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(result->as.invalid.message, sizeof(result->as.invalid.message), format, arguments);
	va_end(arguments);
}

static bool parse_integer(const char *text, long minimum, long maximum, int *parsed_value)
{
	if (text[0] == '\0')
		return false;
	char *end_of_number = NULL;
	errno = 0;
	const long value = strtol(text, &end_of_number, 10);
	if (errno != 0 || *end_of_number != '\0' || value < minimum || value > maximum)
		return false;
	*parsed_value = (int) value;
	return true;
}

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

static void apply_flag(parser_state_t *state, flag_option_t flag)
{
	switch (flag) {
		case FLAG_HELP:
			state->result.kind = COMMAND_LINE_SHOW_HELP;
			return;
		case FLAG_VERSION:
			state->result.kind = COMMAND_LINE_SHOW_VERSION;
			return;
	}
}

/* `option_as_written` is "-x" or "--long-name", for diagnostics. */
static void apply_valued(parser_state_t *state, valued_option_t option, const char *option_as_written, const char *value)
{
	command_line_t *result = &state->result;
	run_options_t *run_options = &result->as.run;
	switch (option) {
		case VALUED_MAPPING_COUNT:
			if (!parse_integer(value, INT_MIN, INT_MAX, &run_options->mapping_count))
				set_invalid(result, "invalid value '%s' for %s: expected an integer", value, option_as_written);
			return;
		case VALUED_TIMEOUT:
			if (!parse_integer(value, 0, INT_MAX, &run_options->timeout_in_seconds))
				set_invalid(result, "invalid value '%s' for %s: expected a non-negative integer", value, option_as_written);
			return;
		case VALUED_CONFIG:
			run_options->config_path = value;
			return;
		case VALUED_REDIRECT:
			run_options->redirect_path = value;
			return;
		case VALUED_STATUS_FIFO:
			run_options->status_fifo_path = value;
			return;
		case VALUED_ABORT_KEYSYM:
			run_options->abort_keysym_name = value;
			return;
		case VALUED_INDICATOR:
			if (!indicator_parse_position(value, &state->indicator_config.position)) {
				char position_names[MAXLEN] = "";
				append_position_names(position_names, sizeof(position_names));
				set_invalid(result, "invalid value '%s' for %s: expected %s", value, option_as_written, position_names);
				return;
			}
			state->indicator_position_given = true;
			return;
		case VALUED_INDICATOR_FONT:
			if (value[0] == '\0') {
				set_invalid(result, "invalid value '' for %s: expected a Pango font description", option_as_written);
				return;
			}
			state->indicator_config.font_description_text = value;
			state->indicator_look_given = true;
			return;
		case VALUED_INDICATOR_FOREGROUND:
			if (!indicator_parse_rgba_color(value, &state->indicator_config.foreground_color)) {
				set_invalid(result, "invalid value '%s' for %s: expected a color as #rrggbb or #rrggbbaa", value, option_as_written);
				return;
			}
			state->indicator_look_given = true;
			return;
		case VALUED_INDICATOR_BACKGROUND:
			if (!indicator_parse_rgba_color(value, &state->indicator_config.background_color)) {
				set_invalid(result, "invalid value '%s' for %s: expected a color as #rrggbb or #rrggbbaa", value, option_as_written);
				return;
			}
			state->indicator_look_given = true;
			return;
	}
}

static void initialize_parser_state(parser_state_t *state)
{
	state->result.kind = COMMAND_LINE_RUN;
	run_options_t *run_options = &state->result.as.run;
	run_options->mapping_count = DEFAULT_MAPPING_COUNT;
	run_options->timeout_in_seconds = DEFAULT_CHAIN_TIMEOUT_IN_SECONDS;
	run_options->config_path = NULL;
	run_options->redirect_path = NULL;
	run_options->status_fifo_path = NULL;
	run_options->abort_keysym_name = NULL;
	run_options->indicator.kind = INDICATOR_SETTINGS_DISABLED;
	run_options->indicator_look_given_without_position = false;
	run_options->extra_config_count = 0;
	run_options->extra_config_paths = NULL;

	state->indicator_config.position = INDICATOR_POSITION_TOP_RIGHT;
	state->indicator_config.font_description_text = INDICATOR_DEFAULT_FONT_DESCRIPTION;
	/* The built-in colors are constants verified by the unit test; a failure
	 * here would be a programming error, not user input. */
	if (!indicator_parse_rgba_color(INDICATOR_DEFAULT_FOREGROUND_COLOR, &state->indicator_config.foreground_color)
			|| !indicator_parse_rgba_color(INDICATOR_DEFAULT_BACKGROUND_COLOR, &state->indicator_config.background_color))
		set_invalid(&state->result, "the built-in indicator colors are invalid");
	state->indicator_position_given = false;
	state->indicator_look_given = false;
}

static void finalize_run_options(parser_state_t *state, char **arguments, int extra_config_count)
{
	run_options_t *run_options = &state->result.as.run;
	if (state->indicator_position_given) {
		run_options->indicator.kind = INDICATOR_SETTINGS_ENABLED;
		run_options->indicator.as.enabled = state->indicator_config;
	} else {
		run_options->indicator.kind = INDICATOR_SETTINGS_DISABLED;
	}
	run_options->indicator_look_given_without_position = state->indicator_look_given && !state->indicator_position_given;
	run_options->extra_config_count = extra_config_count;
	run_options->extra_config_paths = arguments + 1;
}

/* Applies one option. `inline_value` is the value attached to the option
 * itself ("--name=VALUE" or "-xVALUE"), NULL if there was none; a valued
 * option without one consumes the next argument. Advances `*index` when it
 * does. Leaves the result in a non-RUN state to stop parsing. */
static void apply_spec(parser_state_t *state, const option_spec_t *spec, const char *option_as_written, const char *inline_value, char **arguments, int argument_count, int *index)
{
	switch (spec->kind) {
		case OPTION_KIND_FLAG:
			if (inline_value != NULL) {
				set_invalid(&state->result, "option '%s' does not take a value", option_as_written);
				return;
			}
			apply_flag(state, spec->as.flag);
			return;
		case OPTION_KIND_VALUED:
			if (inline_value != NULL) {
				apply_valued(state, spec->as.valued.id, option_as_written, inline_value);
			} else if (*index + 1 < argument_count) {
				*index += 1;
				apply_valued(state, spec->as.valued.id, option_as_written, arguments[*index]);
			} else {
				set_invalid(&state->result, "option '%s' requires an argument %s", option_as_written, spec->as.valued.argument_name);
			}
			return;
	}
}

command_line_t parse_command_line(int argument_count, char **arguments)
{
	parser_state_t state;
	initialize_parser_state(&state);
	if (state.result.kind != COMMAND_LINE_RUN)
		return state.result;

	/* Non-option arguments are compacted towards arguments[1]. The target slot
	 * never exceeds the index being read, so no unread entry is overwritten. */
	int next_extra_slot = 1;
	bool options_ended = false;

	for (int index = 1; index < argument_count; index++) {
		char *argument = arguments[index];
		const bool looks_like_option = argument[0] == '-' && argument[1] != '\0';
		if (options_ended || !looks_like_option) {
			arguments[next_extra_slot] = argument;
			next_extra_slot++;
			continue;
		}
		if (strcmp(argument, "--") == 0) {
			options_ended = true;
			continue;
		}

		if (argument[1] == '-') {
			const char *long_name = argument + 2;
			const char *equals_sign = strchr(long_name, '=');
			const size_t long_name_length = (equals_sign != NULL) ? (size_t) (equals_sign - long_name) : strlen(long_name);
			const option_spec_t *spec = find_spec_by_long_name(long_name, long_name_length);
			if (spec == NULL) {
				set_invalid(&state.result, "unrecognized option '%s'", argument);
				return state.result;
			}
			char option_as_written[MAXLEN];
			snprintf(option_as_written, sizeof(option_as_written), "--%s", spec->long_name);
			const char *inline_value = (equals_sign != NULL) ? equals_sign + 1 : NULL;
			apply_spec(&state, spec, option_as_written, inline_value, arguments, argument_count, &index);
			if (state.result.kind != COMMAND_LINE_RUN)
				return state.result;
		} else {
			/* A cluster of short options: flags may be chained; the first valued
			 * option consumes the rest of the cluster (or the next argument). */
			for (const char *cursor = argument + 1; *cursor != '\0'; cursor++) {
				const option_spec_t *spec = find_spec_by_short_name(*cursor);
				if (spec == NULL) {
					set_invalid(&state.result, "invalid option -- '%c'", *cursor);
					return state.result;
				}
				const char option_as_written[3] = {'-', *cursor, '\0'};
				const char *inline_value = NULL;
				bool rest_of_cluster_consumed = false;
				switch (spec->kind) {
					case OPTION_KIND_FLAG:
						break;
					case OPTION_KIND_VALUED:
						rest_of_cluster_consumed = true;
						if (cursor[1] != '\0')
							inline_value = cursor + 1;
						break;
				}
				apply_spec(&state, spec, option_as_written, inline_value, arguments, argument_count, &index);
				if (state.result.kind != COMMAND_LINE_RUN)
					return state.result;
				if (rest_of_cluster_consumed)
					break;
			}
		}
	}

	finalize_run_options(&state, arguments, next_extra_slot - 1);
	return state.result;
}

void print_usage(FILE *output)
{
	fprintf(output, "Usage: sxhkd [OPTION]... [EXTRA_CONFIG]...\n");
	fprintf(output, "Simple X hotkey daemon: runs commands on key chords and chord chains.\n\n");
	fprintf(output, "Options:\n");

	char option_names[LENGTH(option_specs)][MAXLEN];
	size_t column_width = 0;
	for (size_t index = 0; index < LENGTH(option_specs); index++) {
		const option_spec_t *spec = &option_specs[index];
		switch (spec->kind) {
			case OPTION_KIND_FLAG:
				snprintf(option_names[index], sizeof(option_names[index]), "  -%c, --%s", spec->short_name, spec->long_name);
				break;
			case OPTION_KIND_VALUED:
				snprintf(option_names[index], sizeof(option_names[index]), "  -%c, --%s %s", spec->short_name, spec->long_name, spec->as.valued.argument_name);
				break;
		}
		const size_t width = strlen(option_names[index]);
		if (width > column_width)
			column_width = width;
	}
	for (size_t index = 0; index < LENGTH(option_specs); index++)
		fprintf(output, "%-*s  %s\n", (int) column_width, option_names[index], option_specs[index].description);

	char position_names[MAXLEN] = "";
	append_position_names(position_names, sizeof(position_names));
	fprintf(output, "\nPositions for --indicator:\n  %s.\n", position_names);
	fprintf(output, "Long options also accept --name=VALUE; -- ends the options.\n");
	fprintf(output, "See sxhkd(1) for the configuration syntax and the status FIFO protocol.\n");
}
