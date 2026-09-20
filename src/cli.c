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

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"

static const cli_option_spec_t *find_spec_by_short_name(const cli_table_t *table, char short_name)
{
	for (size_t index = 0; index < table->spec_count; index++) {
		if (table->specs[index].short_name == short_name)
			return &table->specs[index];
	}
	return NULL;
}

static const cli_option_spec_t *find_spec_by_long_name(const cli_table_t *table, const char *long_name, size_t long_name_length)
{
	for (size_t index = 0; index < table->spec_count; index++) {
		const char *candidate = table->specs[index].long_name;
		if (strlen(candidate) == long_name_length && strncmp(candidate, long_name, long_name_length) == 0)
			return &table->specs[index];
	}
	return NULL;
}

cli_apply_result_t cli_apply_invalid(cli_error_t *error, const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(error->message, sizeof(error->message), format, arguments);
	va_end(arguments);
	return CLI_APPLY_INVALID;
}

bool cli_parse_integer(const char *text, int minimum, int maximum, int *parsed_value)
{
	const char *first_digit = (text[0] == '-') ? text + 1 : text;
	if (!isdigit((unsigned char) first_digit[0]))
		return false;
	char *end_of_number = NULL;
	errno = 0;
	const long value = strtol(text, &end_of_number, 10);
	if (errno != 0 || *end_of_number != '\0' || value < minimum || value > maximum)
		return false;
	*parsed_value = (int) value;
	return true;
}

/* Applies one option. `inline_value` is the value attached to the option
 * itself ("--name=VALUE" or "-xVALUE"), NULL if there was none; a valued
 * option without one consumes the next argument. Advances `*index` when it
 * does. The engine's own diagnostics go through the same error as the
 * callbacks', so that the result maps to the outcome in one place. */
static cli_apply_result_t apply_spec(const cli_table_t *table, const cli_option_spec_t *spec, const char *option_as_written, const char *inline_value, char **arguments, int argument_count, int *index, cli_error_t *error)
{
	cli_apply_result_t result = CLI_APPLY_CONTINUE;
	switch (spec->kind) {
		case CLI_OPTION_FLAG:
			if (inline_value != NULL)
				return cli_apply_invalid(error, "option '%s' does not take a value", option_as_written);
			result = table->apply_flag(spec->as.flag.id, option_as_written, table->state, error);
			break;
		case CLI_OPTION_VALUED: {
			const char *value = inline_value;
			if (value == NULL) {
				if (*index + 1 >= argument_count)
					return cli_apply_invalid(error, "option '%s' requires an argument %s", option_as_written, spec->as.valued.argument_name);
				*index += 1;
				value = arguments[*index];
			}
			/* No valued option has a meaningful empty value: an empty path,
			 * keysym or number would only fail later, or silently misbehave. */
			if (value[0] == '\0')
				return cli_apply_invalid(error, "invalid value '' for %s: expected a non-empty %s", option_as_written, spec->as.valued.argument_name);
			result = table->apply_valued(spec->as.valued.id, option_as_written, value, table->state, error);
			break;
		}
	}
	return result;
}

/* Records what applying an option decided; true iff parsing goes on. */
static bool continue_after(cli_outcome_t *outcome, cli_apply_result_t result)
{
	switch (result) {
		case CLI_APPLY_CONTINUE:
			return true;
		case CLI_APPLY_SHOW_HELP:
			outcome->kind = CLI_OUTCOME_SHOW_HELP;
			return false;
		case CLI_APPLY_SHOW_VERSION:
			outcome->kind = CLI_OUTCOME_SHOW_VERSION;
			return false;
		case CLI_APPLY_INVALID:
			outcome->kind = CLI_OUTCOME_INVALID;
			return false;
	}
	return false;
}

cli_outcome_t cli_parse(const cli_table_t *table, int argument_count, char **arguments)
{
	cli_outcome_t outcome;
	outcome.kind = CLI_OUTCOME_RUN;
	/* The error is written in place: it is the outcome's own message once
	 * the kind says so, and dead space in the union otherwise. */
	cli_error_t *error = &outcome.as.invalid;

	/* Non-option arguments are compacted towards arguments[1]. The target slot
	 * never exceeds the index being read, so no unread entry is overwritten. */
	int next_positional_slot = 1;
	bool options_ended = false;

	for (int index = 1; index < argument_count; index++) {
		char *argument = arguments[index];
		const bool looks_like_option = argument[0] == '-' && argument[1] != '\0';
		if (options_ended || !looks_like_option) {
			arguments[next_positional_slot] = argument;
			next_positional_slot++;
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
			const cli_option_spec_t *spec = find_spec_by_long_name(table, long_name, long_name_length);
			if (spec == NULL) {
				continue_after(&outcome, cli_apply_invalid(error, "unrecognized option '%s'", argument));
				return outcome;
			}
			char option_as_written[MAXLEN];
			snprintf(option_as_written, sizeof(option_as_written), "--%s", spec->long_name);
			const char *inline_value = (equals_sign != NULL) ? equals_sign + 1 : NULL;
			if (!continue_after(&outcome, apply_spec(table, spec, option_as_written, inline_value, arguments, argument_count, &index, error)))
				return outcome;
		} else {
			/* A cluster of short options: flags may be chained; the first valued
			 * option consumes the rest of the cluster (or the next argument). */
			for (const char *cursor = argument + 1; *cursor != '\0'; cursor++) {
				const cli_option_spec_t *spec = find_spec_by_short_name(table, *cursor);
				if (spec == NULL) {
					continue_after(&outcome, cli_apply_invalid(error, "invalid option -- '%c'", *cursor));
					return outcome;
				}
				const char option_as_written[3] = {'-', *cursor, '\0'};
				const char *inline_value = NULL;
				bool rest_of_cluster_consumed = false;
				switch (spec->kind) {
					case CLI_OPTION_FLAG:
						break;
					case CLI_OPTION_VALUED:
						rest_of_cluster_consumed = true;
						if (cursor[1] != '\0')
							inline_value = cursor + 1;
						break;
				}
				if (!continue_after(&outcome, apply_spec(table, spec, option_as_written, inline_value, arguments, argument_count, &index, error)))
					return outcome;
				if (rest_of_cluster_consumed)
					break;
			}
		}
	}

	outcome.as.run.positional_count = next_positional_slot - 1;
	outcome.as.run.positionals = arguments + 1;
	return outcome;
}

/* Formats the name column of one row, e.g. "  -c, --config FILE". */
static void format_option_names(const cli_option_spec_t *spec, char *buffer, size_t capacity)
{
	switch (spec->kind) {
		case CLI_OPTION_FLAG:
			snprintf(buffer, capacity, "  -%c, --%s", spec->short_name, spec->long_name);
			break;
		case CLI_OPTION_VALUED:
			snprintf(buffer, capacity, "  -%c, --%s %s", spec->short_name, spec->long_name, spec->as.valued.argument_name);
			break;
	}
}

void cli_print_help(FILE *output, const cli_help_t *help)
{
	fprintf(output, "Usage: %s %s\n", help->program_name, help->usage_arguments);
	fprintf(output, "%s\n\n", help->summary);
	fprintf(output, "Options:\n");

	/* Two passes over a single buffer: the column width first, then the rows. */
	char option_names[MAXLEN];
	size_t column_width = 0;
	for (size_t index = 0; index < help->spec_count; index++) {
		format_option_names(&help->specs[index], option_names, sizeof(option_names));
		const size_t width = strlen(option_names);
		if (width > column_width)
			column_width = width;
	}
	for (size_t index = 0; index < help->spec_count; index++) {
		format_option_names(&help->specs[index], option_names, sizeof(option_names));
		fprintf(output, "%-*s  %s\n", (int) column_width, option_names, help->specs[index].description);
	}

	fprintf(output, "\n");
	if (help->notes != NULL)
		fprintf(output, "%s", help->notes);
	fprintf(output, "Long options also accept --name=VALUE; -- ends the options.\n");
	if (help->epilogue != NULL)
		fprintf(output, "%s", help->epilogue);
}
