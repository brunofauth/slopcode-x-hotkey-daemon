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

/* Headless unit test for src/cli.c: the engine against a small synthetic
 * table, independent of any program's real options. Built and run by
 * `make check`. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"

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

#define LENGTH(x) (sizeof(x) / sizeof(*x))

/* The synthetic program: two stop flags, one counted flag, a number and a
 * path. The counted flag fails on its third occurrence, to exercise a
 * diagnostic raised by a flag callback. */
typedef enum {
	TEST_FLAG_HELP,
	TEST_FLAG_VERSION,
	TEST_FLAG_QUIET
} test_flag_t;

typedef enum {
	TEST_VALUED_NUMBER,
	TEST_VALUED_OUTPUT
} test_valued_t;

#define TEST_NUMBER_MAXIMUM 100
#define TEST_QUIET_MAXIMUM  2

static const cli_option_spec_t test_specs[] = {
	CLI_FLAG('h', "help", TEST_FLAG_HELP, "Print this help and exit."),
	CLI_FLAG('v', "version", TEST_FLAG_VERSION, "Print the version and exit."),
	CLI_FLAG('q', "quiet", TEST_FLAG_QUIET, "Be quieter; may be given twice."),
	CLI_VALUED('n', "number", TEST_VALUED_NUMBER, "NUMBER", "A number from 0 to 100."),
	CLI_VALUED('o', "output", TEST_VALUED_OUTPUT, "FILE", "Write to FILE."),
};

typedef struct {
	int quiet_count;
	int number;
	const char *output;   /* NULL: not given */
} test_state_t;

static const test_state_t initial_state = {0, -1, NULL};

static cli_apply_result_t test_apply_flag(int flag_id, const char *option_as_written, void *state, cli_error_t *error)
{
	test_state_t *test_state = state;
	cli_apply_result_t result = CLI_APPLY_CONTINUE;
	switch ((test_flag_t) flag_id) {
		case TEST_FLAG_HELP:
			result = CLI_APPLY_SHOW_HELP;
			break;
		case TEST_FLAG_VERSION:
			result = CLI_APPLY_SHOW_VERSION;
			break;
		case TEST_FLAG_QUIET:
			test_state->quiet_count++;
			if (test_state->quiet_count > TEST_QUIET_MAXIMUM)
				result = cli_apply_invalid(error, "option '%s' given more than %d times", option_as_written, TEST_QUIET_MAXIMUM);
			break;
	}
	return result;
}

static cli_apply_result_t test_apply_valued(int valued_id, const char *option_as_written, const char *value, void *state, cli_error_t *error)
{
	test_state_t *test_state = state;
	cli_apply_result_t result = CLI_APPLY_CONTINUE;
	switch ((test_valued_t) valued_id) {
		case TEST_VALUED_NUMBER:
			if (!cli_parse_integer(value, 0, TEST_NUMBER_MAXIMUM, &test_state->number))
				result = cli_apply_invalid(error, "invalid value '%s' for %s: expected an integer from 0 to %d", value, option_as_written, TEST_NUMBER_MAXIMUM);
			break;
		case TEST_VALUED_OUTPUT:
			test_state->output = value;
			break;
	}
	return result;
}

static cli_outcome_t parse_with_table(test_state_t *state, int argument_count, char **arguments)
{
	*state = initial_state;
	const cli_table_t table = {
		.specs = test_specs,
		.spec_count = LENGTH(test_specs),
		.state = state,
		.apply_flag = test_apply_flag,
		.apply_valued = test_apply_valued,
	};
	return cli_parse(&table, argument_count, arguments);
}

/* Builds a mutable argv from string literals; the parser may reorder it.
 * The state is reset before every parse. */
#define PARSE(state_pointer, ...) \
	parse_with_table(state_pointer, (int) (sizeof((char *[]){"clitest", __VA_ARGS__}) / sizeof(char *)), (char *[]){"clitest", __VA_ARGS__})

static bool message_is(cli_outcome_t outcome, const char *expected)
{
	if (outcome.kind != CLI_OUTCOME_INVALID)
		return false;
	if (strcmp(outcome.as.invalid.message, expected) != 0) {
		fprintf(stderr, "message \"%s\" differs from \"%s\"\n", outcome.as.invalid.message, expected);
		return false;
	}
	return true;
}

static void test_no_arguments(void)
{
	test_state_t state;
	char *arguments[] = {"clitest"};
	const cli_outcome_t outcome = parse_with_table(&state, 1, arguments);
	CHECK(outcome.kind == CLI_OUTCOME_RUN);
	CHECK(outcome.as.run.positional_count == 0);
	CHECK(outcome.as.run.positionals == arguments + 1);
	CHECK(state.quiet_count == 0 && state.number == -1 && state.output == NULL);
}

static void test_stop_flags(void)
{
	test_state_t state;
	CHECK(PARSE(&state, "-h").kind == CLI_OUTCOME_SHOW_HELP);
	CHECK(PARSE(&state, "--help").kind == CLI_OUTCOME_SHOW_HELP);
	CHECK(PARSE(&state, "-v").kind == CLI_OUTCOME_SHOW_VERSION);
	CHECK(PARSE(&state, "--version").kind == CLI_OUTCOME_SHOW_VERSION);
	CHECK(PARSE(&state, "-vh").kind == CLI_OUTCOME_SHOW_VERSION);   /* the first one wins */
	CHECK(PARSE(&state, "-hv").kind == CLI_OUTCOME_SHOW_HELP);
	CHECK(PARSE(&state, "-n", "5", "--help").kind == CLI_OUTCOME_SHOW_HELP);
	/* A stop flag ends the parse: what follows is not applied. */
	CHECK(PARSE(&state, "--help", "-n", "5").kind == CLI_OUTCOME_SHOW_HELP && state.number == -1);
	CHECK(PARSE(&state, "--help", "--bogus").kind == CLI_OUTCOME_SHOW_HELP);
	CHECK(message_is(PARSE(&state, "--help=yes"), "option '--help' does not take a value"));
	CHECK(message_is(PARSE(&state, "--quiet="), "option '--quiet' does not take a value"));
}

static void test_counted_flag(void)
{
	test_state_t state;
	CHECK(PARSE(&state, "-q").kind == CLI_OUTCOME_RUN && state.quiet_count == 1);
	CHECK(PARSE(&state, "-q", "--quiet").kind == CLI_OUTCOME_RUN && state.quiet_count == 2);
	CHECK(PARSE(&state, "-qq").kind == CLI_OUTCOME_RUN && state.quiet_count == 2);
	/* A diagnostic raised by a flag callback, with the option as written. */
	CHECK(message_is(PARSE(&state, "-qqq"), "option '-q' given more than 2 times"));
	CHECK(message_is(PARSE(&state, "-qq", "--quiet"), "option '--quiet' given more than 2 times"));
}

static void test_valued_forms(void)
{
	test_state_t state;
	CHECK(PARSE(&state, "-n", "5").kind == CLI_OUTCOME_RUN && state.number == 5);
	CHECK(PARSE(&state, "-n5").kind == CLI_OUTCOME_RUN && state.number == 5);
	CHECK(PARSE(&state, "--number=5").kind == CLI_OUTCOME_RUN && state.number == 5);
	CHECK(PARSE(&state, "--number", "5").kind == CLI_OUTCOME_RUN && state.number == 5);
	CHECK(PARSE(&state, "-o", "out").kind == CLI_OUTCOME_RUN && strcmp(state.output, "out") == 0);
	CHECK(PARSE(&state, "-oout").kind == CLI_OUTCOME_RUN && strcmp(state.output, "out") == 0);
	CHECK(PARSE(&state, "--output=out").kind == CLI_OUTCOME_RUN && strcmp(state.output, "out") == 0);
	CHECK(PARSE(&state, "--output", "out").kind == CLI_OUTCOME_RUN && strcmp(state.output, "out") == 0);
	/* The value keeps its '=' and '-' characters: only the first '=' splits. */
	CHECK(PARSE(&state, "--output=a=b").kind == CLI_OUTCOME_RUN && strcmp(state.output, "a=b") == 0);
	CHECK(PARSE(&state, "-o", "-").kind == CLI_OUTCOME_RUN && strcmp(state.output, "-") == 0);
	/* The last occurrence wins, as the callback simply overwrites. */
	CHECK(PARSE(&state, "-n", "1", "-n", "2").kind == CLI_OUTCOME_RUN && state.number == 2);
	/* The value points into argv itself: no copy is made. */
	char *arguments[] = {"clitest", "--output", "path"};
	const cli_outcome_t outcome = parse_with_table(&state, 3, arguments);
	CHECK(outcome.kind == CLI_OUTCOME_RUN && state.output == arguments[2]);
}

static void test_clusters(void)
{
	test_state_t state;
	/* Flags chain; the first valued option takes the rest of the cluster. */
	CHECK(PARSE(&state, "-qn7").kind == CLI_OUTCOME_RUN && state.quiet_count == 1 && state.number == 7);
	CHECK(PARSE(&state, "-qqo", "file").kind == CLI_OUTCOME_RUN && state.quiet_count == 2 && strcmp(state.output, "file") == 0);
	CHECK(PARSE(&state, "-qoq").kind == CLI_OUTCOME_RUN && state.quiet_count == 1 && strcmp(state.output, "q") == 0);
	CHECK(PARSE(&state, "-qh").kind == CLI_OUTCOME_SHOW_HELP && state.quiet_count == 1);
	/* The rest of the cluster is a value even when it looks like an option. */
	CHECK(PARSE(&state, "-o-h").kind == CLI_OUTCOME_RUN && strcmp(state.output, "-h") == 0);
	CHECK(message_is(PARSE(&state, "-nz"), "invalid value 'z' for -n: expected an integer from 0 to 100"));
	CHECK(message_is(PARSE(&state, "-qz"), "invalid option -- 'z'"));
}

static void test_missing_arguments(void)
{
	test_state_t state;
	CHECK(message_is(PARSE(&state, "--number"), "option '--number' requires an argument NUMBER"));
	CHECK(message_is(PARSE(&state, "-n"), "option '-n' requires an argument NUMBER"));
	CHECK(message_is(PARSE(&state, "--output"), "option '--output' requires an argument FILE"));
	CHECK(message_is(PARSE(&state, "-o"), "option '-o' requires an argument FILE"));
	CHECK(message_is(PARSE(&state, "-q", "-o"), "option '-o' requires an argument FILE"));
	/* The next argument is consumed as the value even when it looks like an
	 * option, so the value is not missing here. */
	CHECK(PARSE(&state, "-o", "--help").kind == CLI_OUTCOME_RUN && strcmp(state.output, "--help") == 0);
	CHECK(PARSE(&state, "--output", "--").kind == CLI_OUTCOME_RUN && strcmp(state.output, "--") == 0);
}

/* An empty value is rejected for every valued option, in one place, before
 * the callback sees it. */
static void test_empty_values(void)
{
	test_state_t state;
	CHECK(message_is(PARSE(&state, "--number="), "invalid value '' for --number: expected a non-empty NUMBER"));
	CHECK(message_is(PARSE(&state, "--output="), "invalid value '' for --output: expected a non-empty FILE"));
	/* The separate-argument form: "" is not an option, so it is consumed as the value. */
	CHECK(message_is(PARSE(&state, "-n", ""), "invalid value '' for -n: expected a non-empty NUMBER"));
	CHECK(message_is(PARSE(&state, "-o", ""), "invalid value '' for -o: expected a non-empty FILE"));
	CHECK(message_is(PARSE(&state, "--output", ""), "invalid value '' for --output: expected a non-empty FILE"));
	CHECK(state.output == NULL);   /* the callback never saw it */
}

static void test_unknown_options(void)
{
	test_state_t state;
	CHECK(message_is(PARSE(&state, "--bogus"), "unrecognized option '--bogus'"));
	CHECK(message_is(PARSE(&state, "--num=5"), "unrecognized option '--num=5'"));   /* no abbreviations */
	CHECK(message_is(PARSE(&state, "--numbers"), "unrecognized option '--numbers'"));
	CHECK(message_is(PARSE(&state, "-z"), "invalid option -- 'z'"));
	CHECK(message_is(PARSE(&state, "-N"), "invalid option -- 'N'"));   /* case matters */
	CHECK(message_is(PARSE(&state, "---help"), "unrecognized option '---help'"));
	/* Parsing stops at the first diagnostic: nothing after it is applied. */
	CHECK(PARSE(&state, "--bogus", "-n", "5").kind == CLI_OUTCOME_INVALID && state.number == -1);
}

static void test_callback_diagnostics(void)
{
	test_state_t state;
	CHECK(message_is(PARSE(&state, "-n", "101"), "invalid value '101' for -n: expected an integer from 0 to 100"));
	CHECK(message_is(PARSE(&state, "--number=-1"), "invalid value '-1' for --number: expected an integer from 0 to 100"));
	CHECK(message_is(PARSE(&state, "--number", "x"), "invalid value 'x' for --number: expected an integer from 0 to 100"));
	/* The option is reported as written, the long name canonically. */
	CHECK(message_is(PARSE(&state, "-n1.5"), "invalid value '1.5' for -n: expected an integer from 0 to 100"));
}

static void test_positionals_and_permutation(void)
{
	test_state_t state;
	char *arguments[] = {"clitest", "a", "-n", "5", "b", "--", "-n", "-"};
	cli_outcome_t outcome = parse_with_table(&state, (int) LENGTH(arguments), arguments);
	CHECK(outcome.kind == CLI_OUTCOME_RUN);
	CHECK(state.number == 5);
	CHECK(outcome.as.run.positional_count == 4);
	CHECK(outcome.as.run.positionals == arguments + 1);
	CHECK(strcmp(outcome.as.run.positionals[0], "a") == 0);
	CHECK(strcmp(outcome.as.run.positionals[1], "b") == 0);
	CHECK(strcmp(outcome.as.run.positionals[2], "-n") == 0);
	CHECK(strcmp(outcome.as.run.positionals[3], "-") == 0);
	/* argv[0] is untouched. */
	CHECK(strcmp(arguments[0], "clitest") == 0);

	/* A lone "-" is a positional; "--" itself is dropped. */
	outcome = PARSE(&state, "-", "--", "--");
	CHECK(outcome.kind == CLI_OUTCOME_RUN && outcome.as.run.positional_count == 2);
	CHECK(strcmp(outcome.as.run.positionals[0], "-") == 0 && strcmp(outcome.as.run.positionals[1], "--") == 0);

	/* After "--" nothing is an option, not even a stop flag. */
	outcome = PARSE(&state, "--", "--help");
	CHECK(outcome.kind == CLI_OUTCOME_RUN && outcome.as.run.positional_count == 1 && strcmp(outcome.as.run.positionals[0], "--help") == 0);

	/* Options interleaved with positionals in every position. */
	outcome = PARSE(&state, "-q", "x", "-o", "out", "y", "-q", "z");
	CHECK(outcome.kind == CLI_OUTCOME_RUN && outcome.as.run.positional_count == 3);
	CHECK(strcmp(outcome.as.run.positionals[0], "x") == 0 && strcmp(outcome.as.run.positionals[1], "y") == 0 && strcmp(outcome.as.run.positionals[2], "z") == 0);
	CHECK(state.quiet_count == 2 && strcmp(state.output, "out") == 0);
}

static void test_parse_integer(void)
{
	int value = -99;
	CHECK(cli_parse_integer("5", 0, 100, &value) && value == 5);
	CHECK(cli_parse_integer("0", 0, 100, &value) && value == 0);
	CHECK(cli_parse_integer("100", 0, 100, &value) && value == 100);   /* bounds are inclusive */
	CHECK(cli_parse_integer("-1", -1, 100, &value) && value == -1);
	CHECK(cli_parse_integer("007", 0, 100, &value) && value == 7);

	value = -99;
	CHECK(!cli_parse_integer("101", 0, 100, &value));
	CHECK(!cli_parse_integer("-1", 0, 100, &value));
	CHECK(!cli_parse_integer("-2", -1, 100, &value));
	/* Exact grammar: strtol would accept a leading '+' or whitespace. */
	CHECK(!cli_parse_integer("+5", 0, 100, &value));
	CHECK(!cli_parse_integer(" 5", 0, 100, &value));
	CHECK(!cli_parse_integer("5 ", 0, 100, &value));
	CHECK(!cli_parse_integer("", 0, 100, &value));
	CHECK(!cli_parse_integer("-", -1, 100, &value));
	CHECK(!cli_parse_integer("- 1", -1, 100, &value));
	CHECK(!cli_parse_integer("1.5", 0, 100, &value));
	CHECK(!cli_parse_integer("0x10", 0, 100, &value));
	CHECK(!cli_parse_integer("99999999999", 0, 100, &value));
	CHECK(!cli_parse_integer("99999999999999999999", 0, 100, &value));   /* out of range for strtol itself */
	CHECK(value == -99);   /* never written on failure */
}

/* Reads the whole file into a NUL-terminated buffer; false if it does not fit. */
static bool read_all(FILE *file, char *buffer, size_t capacity)
{
	rewind(file);
	const size_t length = fread(buffer, 1, capacity - 1, file);
	buffer[length] = '\0';
	return length < capacity - 1;
}

static void test_print_help(void)
{
	FILE *output = tmpfile();
	CHECK(output != NULL);
	if (output == NULL)
		return;

	cli_help_t help = {
		.program_name = "clitest",
		.usage_arguments = "[OPTION]... [FILE]...",
		.summary = "A synthetic program.",
		.specs = test_specs,
		.spec_count = LENGTH(test_specs),
		.notes = "Numbers:\n  0 to 100.\n",
		.epilogue = "See clitest(1).\n",
	};
	cli_print_help(output, &help);
	char text[2048];
	CHECK(read_all(output, text, sizeof(text)));
	/* The name column is as wide as its widest row plus two spaces. */
	static const char expected[] =
		"Usage: clitest [OPTION]... [FILE]...\n"
		"A synthetic program.\n"
		"\n"
		"Options:\n"
		"  -h, --help           Print this help and exit.\n"
		"  -v, --version        Print the version and exit.\n"
		"  -q, --quiet          Be quieter; may be given twice.\n"
		"  -n, --number NUMBER  A number from 0 to 100.\n"
		"  -o, --output FILE    Write to FILE.\n"
		"\n"
		"Numbers:\n"
		"  0 to 100.\n"
		"Long options also accept --name=VALUE; -- ends the options.\n"
		"See clitest(1).\n";
	if (strcmp(text, expected) != 0)
		fprintf(stderr, "help text:\n%s", text);
	CHECK(strcmp(text, expected) == 0);
	fclose(output);

	/* Without notes and epilogue, the blank line and the conventions line
	 * still follow the table. */
	output = tmpfile();
	CHECK(output != NULL);
	if (output == NULL)
		return;
	help.notes = NULL;
	help.epilogue = NULL;
	cli_print_help(output, &help);
	CHECK(read_all(output, text, sizeof(text)));
	static const char expected_ending[] =
		"  -o, --output FILE    Write to FILE.\n"
		"\n"
		"Long options also accept --name=VALUE; -- ends the options.\n";
	CHECK(strlen(text) >= strlen(expected_ending) && strcmp(text + strlen(text) - strlen(expected_ending), expected_ending) == 0);

	size_t longest_line = 0;
	for (const char *line = text; *line != '\0';) {
		const size_t length = strcspn(line, "\n");
		if (length > longest_line)
			longest_line = length;
		line += length + (line[length] == '\n' ? 1 : 0);
	}
	CHECK(longest_line <= 110);
	fclose(output);
}

int main(void)
{
	test_no_arguments();
	test_stop_flags();
	test_counted_flag();
	test_valued_forms();
	test_clusters();
	test_missing_arguments();
	test_empty_values();
	test_unknown_options();
	test_callback_diagnostics();
	test_positionals_and_permutation();
	test_parse_integer();
	test_print_help();
	printf("cli_test: %u passed, %u failed\n", passed_check_count, failed_check_count);
	return failed_check_count == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
