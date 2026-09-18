/* Headless unit test for src/options.c. Built and run by `make check`. */

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
	CHECK(message_contains(PARSE("--help=yes"), "does not take a value"));
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
	command_line = PARSE("--timeout");
	CHECK(message_contains(command_line, "requires an argument SECONDS"));
	command_line = PARSE("-t");
	CHECK(message_contains(command_line, "'-t' requires an argument"));
	CHECK(message_contains(PARSE("-t", "99999999999"), "--timeout") || message_contains(PARSE("-t", "99999999999"), "-t"));
}

static void test_mapping_count(void)
{
	CHECK(PARSE("-m", "-1").as.run.mapping_count == -1);
	CHECK(PARSE("--mapping-count=7").as.run.mapping_count == 7);
	CHECK(message_contains(PARSE("-m", "1.5"), "expected an integer"));
}

static void test_unknown_options(void)
{
	CHECK(message_contains(PARSE("--bogus"), "unrecognized option '--bogus'"));
	CHECK(message_contains(PARSE("--time=5"), "unrecognized option"));   /* no abbreviations */
	CHECK(message_contains(PARSE("-z"), "invalid option -- 'z'"));
	CHECK(message_contains(PARSE("-tz"), "invalid value 'z' for -t"));
}

static void test_extra_configs_and_permutation(void)
{
	command_line_t command_line = PARSE("a.conf", "-t", "5", "b.conf", "--", "-t", "-");
	CHECK(command_line.kind == COMMAND_LINE_RUN);
	CHECK(command_line.as.run.timeout_in_seconds == 5);
	CHECK(command_line.as.run.extra_config_count == 4);
	CHECK(strcmp(command_line.as.run.extra_config_paths[0], "a.conf") == 0);
	CHECK(strcmp(command_line.as.run.extra_config_paths[1], "b.conf") == 0);
	CHECK(strcmp(command_line.as.run.extra_config_paths[2], "-t") == 0);
	CHECK(strcmp(command_line.as.run.extra_config_paths[3], "-") == 0);

	command_line = PARSE("-c", "main.conf", "extra.conf");
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
	CHECK(message_contains(PARSE("--indicator-font="), "Pango font description"));
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
	test_unknown_options();
	test_extra_configs_and_permutation();
	test_paths_and_keysym();
	test_indicator();
	test_print_usage();
	printf("options_test: %u passed, %u failed\n", passed_check_count, failed_check_count);
	return failed_check_count == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
