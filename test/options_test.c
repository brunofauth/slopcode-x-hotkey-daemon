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

/* Headless unit test for src/options.c: the daemon's table, typed results
 * and diagnostics. The engine itself (every form, clusters, permutation,
 * the generic diagnostics) is covered by test/cli_test.c against a
 * synthetic table. Built and run by `make check`. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "options.h"

static unsigned int failed_check_count = 0;
static unsigned int passed_check_count = 0;

#define CHECK(condition) \
	do { \
		if (condition) { \
			passed_check_count++; \
		} else { \
			failed_check_count++; \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

/* Builds a mutable argv from string literals; the parser may reorder it. */
#define PARSE(...) \
	parse_command_line((int) (sizeof((char *[]){"sxhkd", __VA_ARGS__}) / sizeof(char *)), (char *[]){"sxhkd", __VA_ARGS__})

static bool message_contains(command_line_t command_line, const char *needle)
{
	return command_line.kind == COMMAND_LINE_INVALID && strstr(command_line.as.invalid.message, needle) != NULL;
}

static void test_defaults(void)
{
	char *arguments[] = {"sxhkd"};
	const command_line_t command_line = parse_command_line(1, arguments);
	CHECK(command_line.kind == COMMAND_LINE_RUN);
	const run_options_t *run = &command_line.as.run;
	CHECK(run->mapping_count == 0);
	CHECK(run->timeout_in_seconds == 3);
	CHECK(run->config_path == NULL && run->redirect_path == NULL && run->status_fifo_path == NULL && run->abort_keysym_name == NULL);
	CHECK(run->indicator.kind == INDICATOR_SETTINGS_DISABLED);
	CHECK(!run->indicator_look_given_without_position);
	CHECK(run->extra_config_count == 0);
}

static void test_help_and_version(void)
{
	CHECK(PARSE("-h").kind == COMMAND_LINE_SHOW_HELP);
	CHECK(PARSE("--help").kind == COMMAND_LINE_SHOW_HELP);
	CHECK(PARSE("-v").kind == COMMAND_LINE_SHOW_VERSION);
	CHECK(PARSE("--version").kind == COMMAND_LINE_SHOW_VERSION);
	CHECK(PARSE("-vh").kind == COMMAND_LINE_SHOW_VERSION);   /* first one wins */
	CHECK(PARSE("-hv").kind == COMMAND_LINE_SHOW_HELP);
	CHECK(PARSE("-t", "5", "--help").kind == COMMAND_LINE_SHOW_HELP);
}

static void test_timeout_forms(void)
{
	CHECK(PARSE("-t", "5").as.run.timeout_in_seconds == 5);
	CHECK(PARSE("-t5").as.run.timeout_in_seconds == 5);
	CHECK(PARSE("--timeout=5").as.run.timeout_in_seconds == 5);
	CHECK(PARSE("--timeout", "5").as.run.timeout_in_seconds == 5);
	CHECK(PARSE("--timeout=0").as.run.timeout_in_seconds == 0);

	command_line_t command_line = PARSE("-t", "x");
	CHECK(message_contains(command_line, "-t") && message_contains(command_line, "'x'"));
	command_line = PARSE("--timeout=-1");
	CHECK(message_contains(command_line, "--timeout") && message_contains(command_line, "non-negative"));
	command_line = PARSE("--timeout=");
	CHECK(message_contains(command_line, "--timeout"));
	CHECK(message_contains(PARSE("-t", "99999999999"), "--timeout") || message_contains(PARSE("-t", "99999999999"), "-t"));
	/* Exact grammar: strtol would accept a leading '+' or whitespace. */
	CHECK(message_contains(PARSE("-t", "+5"), "invalid value '+5' for -t"));
	CHECK(message_contains(PARSE("-t", " 5"), "invalid value ' 5' for -t"));
	CHECK(message_contains(PARSE("-t", "5 "), "invalid value '5 ' for -t"));
}

static void test_mapping_count(void)
{
	CHECK(PARSE("-m", "-1").as.run.mapping_count == -1);
	CHECK(PARSE("-m", "0").as.run.mapping_count == 0);
	CHECK(PARSE("-m0").as.run.mapping_count == 0);
	CHECK(PARSE("--mapping-count=7").as.run.mapping_count == 7);
	/* Only -1 means "all": main treats it specially, other negatives are errors. */
	command_line_t command_line = PARSE("-m", "-2");
	CHECK(message_contains(command_line, "invalid value '-2' for -m") && message_contains(command_line, "expected -1 or a non-negative integer"));
	CHECK(message_contains(PARSE("--mapping-count=-100"), "invalid value '-100' for --mapping-count"));
	CHECK(message_contains(PARSE("-m", "1.5"), "expected -1 or a non-negative integer"));
	CHECK(message_contains(PARSE("-m", "+5"), "invalid value '+5' for -m"));
	CHECK(message_contains(PARSE("-m", " 5"), "invalid value ' 5' for -m"));
	CHECK(message_contains(PARSE("-m", "-"), "invalid value '-' for -m"));
	CHECK(message_contains(PARSE("-m", "- 1"), "invalid value '- 1' for -m"));
	CHECK(message_contains(PARSE("-m", "99999999999"), "expected -1 or a non-negative integer"));
}

/* The non-option arguments are the extra configuration files. */
static void test_extra_configs(void)
{
	const command_line_t command_line = PARSE("-c", "main.conf", "extra.conf");
	CHECK(strcmp(command_line.as.run.config_path, "main.conf") == 0);
	CHECK(command_line.as.run.extra_config_count == 1 && strcmp(command_line.as.run.extra_config_paths[0], "extra.conf") == 0);
}

static void test_paths_and_keysym(void)
{
	const command_line_t command_line = PARSE("--config=a", "--redirect", "b", "-s", "c", "--abort-keysym", "q");
	CHECK(command_line.kind == COMMAND_LINE_RUN);
	CHECK(strcmp(command_line.as.run.config_path, "a") == 0);
	CHECK(strcmp(command_line.as.run.redirect_path, "b") == 0);
	CHECK(strcmp(command_line.as.run.status_fifo_path, "c") == 0);
	CHECK(strcmp(command_line.as.run.abort_keysym_name, "q") == 0);
}

/* An empty value is rejected for every valued option, in one place, before
 * the option-specific parser sees it. */
static void test_empty_values(void)
{
	static const struct {
		char *argument;               /* argv is char **; the parser never writes to it */
		const char *option_as_written;
		const char *argument_name;
	} inline_cases[] = {
		{"--config=", "--config", "FILE"},
		{"--redirect=", "--redirect", "FILE"},
		{"--status-fifo=", "--status-fifo", "PATH"},
		{"--abort-keysym=", "--abort-keysym", "KEYSYM"},
		{"--mapping-count=", "--mapping-count", "COUNT"},
		{"--timeout=", "--timeout", "SECONDS"},
		{"--indicator=", "--indicator", "POSITION"},
		{"--indicator-font=", "--indicator-font", "FONT"},
		{"--indicator-foreground=", "--indicator-foreground", "COLOR"},
		{"--indicator-background=", "--indicator-background", "COLOR"},
	};
	for (size_t index = 0; index < sizeof(inline_cases) / sizeof(*inline_cases); index++) {
		const command_line_t command_line = PARSE(inline_cases[index].argument);
		char expected[MAXLEN];
		snprintf(expected, sizeof(expected), "invalid value '' for %s: expected a non-empty %s", inline_cases[index].option_as_written, inline_cases[index].argument_name);
		if (!message_contains(command_line, expected))
			fprintf(stderr, "for %s: expected \"%s\"\n", inline_cases[index].argument, expected);
		CHECK(message_contains(command_line, expected));
	}
	/* The separate-argument form: "" is not an option, so it is consumed as the value. */
	CHECK(message_contains(PARSE("-c", ""), "invalid value '' for -c: expected a non-empty FILE"));
	CHECK(message_contains(PARSE("-r", ""), "invalid value '' for -r: expected a non-empty FILE"));
	CHECK(message_contains(PARSE("-s", ""), "invalid value '' for -s: expected a non-empty PATH"));
	CHECK(message_contains(PARSE("-a", ""), "invalid value '' for -a: expected a non-empty KEYSYM"));
	CHECK(message_contains(PARSE("-f", ""), "invalid value '' for -f: expected a non-empty FONT"));
	CHECK(message_contains(PARSE("-m", ""), "invalid value '' for -m: expected a non-empty COUNT"));
	CHECK(message_contains(PARSE("-t", ""), "invalid value '' for -t: expected a non-empty SECONDS"));
}

static void test_indicator(void)
{
	command_line_t command_line = PARSE("--indicator", "bottom-left", "--indicator-font", "Mono 9", "--indicator-foreground=#010203", "-B#a0b0c0");
	CHECK(command_line.kind == COMMAND_LINE_RUN);
	CHECK(command_line.as.run.indicator.kind == INDICATOR_SETTINGS_ENABLED);
	const indicator_config_t *config = &command_line.as.run.indicator.as.enabled;
	CHECK(config->position == INDICATOR_POSITION_BOTTOM_LEFT);
	CHECK(strcmp(config->font_description_text, "Mono 9") == 0);
	CHECK(config->foreground_color.red == 1 && config->foreground_color.green == 2 && config->foreground_color.blue == 3);
	CHECK(config->background_color.red == 0xa0 && config->background_color.green == 0xb0 && config->background_color.blue == 0xc0);
	CHECK(!command_line.as.run.indicator_look_given_without_position);
	CHECK(config->foreground_color.alpha == 0xff && config->background_color.alpha == 0xff);

	command_line = PARSE("-i", "top", "--indicator-background=#22222280");
	CHECK(command_line.kind == COMMAND_LINE_RUN && command_line.as.run.indicator.as.enabled.background_color.alpha == 0x80);
	CHECK(message_contains(PARSE("-B", "#abc"), "#rrggbb or #rrggbbaa"));

	command_line = PARSE("-i", "top");
	CHECK(command_line.as.run.indicator.kind == INDICATOR_SETTINGS_ENABLED);
	CHECK(strcmp(command_line.as.run.indicator.as.enabled.font_description_text, INDICATOR_DEFAULT_FONT_DESCRIPTION) == 0);
	CHECK(command_line.as.run.indicator.as.enabled.foreground_color.red == 0xff);

	command_line = PARSE("--indicator-foreground", "#000000");
	CHECK(command_line.kind == COMMAND_LINE_RUN);
	CHECK(command_line.as.run.indicator.kind == INDICATOR_SETTINGS_DISABLED);
	CHECK(command_line.as.run.indicator_look_given_without_position);

	command_line = PARSE("-i", "bogus");
	CHECK(message_contains(command_line, "invalid value 'bogus' for -i"));
	CHECK(message_contains(command_line, "top-left") && message_contains(command_line, " or bottom-right"));
	CHECK(message_contains(PARSE("--indicator-background=12345"), "#rrggbb"));
	CHECK(message_contains(PARSE("--indicator-font="), "expected a non-empty FONT"));
}

static void test_print_usage(void)
{
	FILE *output = tmpfile();
	CHECK(output != NULL);
	if (output == NULL)
		return;
	print_usage(output);
	rewind(output);

	static const char *expected_fragments[] = {
		"Usage: sxhkd [OPTION]... [EXTRA_CONFIG]...",
		"-h, --help", "-v, --version", "-m, --mapping-count COUNT", "-t, --timeout SECONDS",
		"-c, --config FILE", "-r, --redirect FILE", "-s, --status-fifo PATH", "-a, --abort-keysym KEYSYM",
		"-i, --indicator POSITION", "-f, --indicator-font FONT", "-F, --indicator-foreground COLOR",
		"-B, --indicator-background COLOR", "default \"monospace 14\"", "default #ffffff", "default #222222",
		"COUNT (>= 0)", "-1: all (default 0)", "0: never (default 3)",
		"Positions for --indicator:",
		"  top, top-left, top-right, center, center-left, center-right, bottom, bottom-left or bottom-right.",
		"sxhkd(1)",
	};
	bool fragment_seen[sizeof(expected_fragments) / sizeof(*expected_fragments)] = {false};
	char line[512];
	size_t longest_line = 0;
	while (fgets(line, sizeof(line), output) != NULL) {
		const size_t length = strcspn(line, "\n");
		if (length > longest_line)
			longest_line = length;
		for (size_t index = 0; index < sizeof(expected_fragments) / sizeof(*expected_fragments); index++) {
			if (strstr(line, expected_fragments[index]) != NULL)
				fragment_seen[index] = true;
		}
	}
	for (size_t index = 0; index < sizeof(expected_fragments) / sizeof(*expected_fragments); index++) {
		if (!fragment_seen[index])
			fprintf(stderr, "help text lacks: %s\n", expected_fragments[index]);
		CHECK(fragment_seen[index]);
	}
	CHECK(longest_line <= 110);
	fclose(output);
}

int main(void)
{
	test_defaults();
	test_help_and_version();
	test_timeout_forms();
	test_mapping_count();
	test_extra_configs();
	test_paths_and_keysym();
	test_empty_values();
	test_indicator();
	test_print_usage();
	printf("options_test: %u passed, %u failed\n", passed_check_count, failed_check_count);
	return failed_check_count == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
