OUT           = sxhkd
INDICATOR_OUT = sxhkd-indicator
VERCMD  ?= git describe --tags 2> /dev/null
VERSION := $(shell $(VERCMD) || cat VERSION)

# The on-screen chain indicator draws with cairo and pango.
PKG_CONFIG       ?= pkg-config
INDICATOR_PKGS    = cairo cairo-xcb pango pangocairo
# Recursively expanded on purpose: pkg-config only runs when a compile or link
# command needs it, so targets such as `check` and `clean` work without it.
# -isystem keeps the strict warnings from firing inside third-party headers.
INDICATOR_CFLAGS  = $(patsubst -I%,-isystem %,$(shell $(PKG_CONFIG) --cflags $(INDICATOR_PKGS)))
INDICATOR_LIBS    = $(shell $(PKG_CONFIG) --libs $(INDICATOR_PKGS))

# `override` keeps the mandatory flags when CFLAGS/CPPFLAGS are given on the
# make command line (as packagers do), not only through the environment.
override CPPFLAGS += -D_POSIX_C_SOURCE=200112L -DVERSION=\"$(VERSION)\" $(INDICATOR_CFLAGS)
override CFLAGS   += -std=c99 -pedantic -Wall -Wextra
LDFLAGS  ?=

# Stricter diagnostics, applied to the chain indicator objects and to the
# analyze/check targets only, so that the historic sources keep building with
# the default flags on every compiler.
STRICT_CFLAGS = -Wshadow -Wconversion -Wsign-conversion -Wstrict-prototypes -Wmissing-prototypes \
                -Wold-style-definition -Wvla -Wswitch-enum -Wcast-qual -Wundef -Wdouble-promotion \
                -Wformat=2 -Wnull-dereference -Wimplicit-fallthrough
TEST_CFLAGS  ?= -fsanitize=address,undefined
# -Werror is opt-in for `check` (make check WERROR=-Werror): a warning added by
# a newer compiler must not break package builds. `analyze` always uses it, as
# the strictness gate.
WERROR       ?=
LDLIBS    = -lxcb -lxcb-keysyms -lxcb-xkb -lxcb-shape $(INDICATOR_LIBS)
# The indicator program neither grabs keys nor uses XKB: only the banner's
# libraries.
INDICATOR_LDLIBS = -lxcb -lxcb-shape $(INDICATOR_LIBS)

PREFIX    ?= /usr/local
BINPREFIX ?= $(PREFIX)/bin
MANPREFIX ?= $(PREFIX)/share/man
DOCPREFIX ?= $(PREFIX)/share/doc/$(OUT)

all: $(OUT) $(INDICATOR_OUT) ## Build sxhkd and sxhkd-indicator (the default)

# Every target followed by "## text" is listed by `make help`: the recipe
# lines below must start with a tab, as in any other rule.
help: ## Show this help message
	@echo "Available targets:"
	@grep -hE '^[a-zA-Z_-]+:.*## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*## "}; {printf "\033[36m%-20s\033[0m %s\n", $$1, $$2}'

debug: override CFLAGS += -O0 -g
debug: override CPPFLAGS += -DDEBUG
debug: $(OUT) $(INDICATOR_OUT) ## Build both programs unoptimised with debugging symbols and DEBUG output

VPATH = src
# Sourcedeps assigns every object to the program(s) it belongs to; the
# objects shared by both (the indicator, its core, the diagnostics, the
# option engine) are listed under each and compiled once.
OBJ           =
INDICATOR_OBJ =

include Sourcedeps

$(sort $(OBJ) $(INDICATOR_OBJ)): Makefile | check-indicator-deps

$(OUT): $(OBJ)

# No built-in rule links a program whose name matches no object (make's is
# `%: %.o`), so unlike $(OUT) this one spells out the built-in recipe.
$(INDICATOR_OUT): $(INDICATOR_OBJ)
	$(CC) $(LDFLAGS) $^ $(LOADLIBES) $(INDICATOR_LDLIBS) -o $@

cli.o diagnostics.o indicator_core.o indicator.o options.o indicator_main.o indicator_options.o indicator_protocol.o: override CFLAGS += $(STRICT_CFLAGS)

check-indicator-deps:
	@$(PKG_CONFIG) --exists $(INDICATOR_PKGS) || { \
		echo "error: building sxhkd needs $(PKG_CONFIG) and the development files of: $(INDICATOR_PKGS)" >&2; \
		exit 1; \
	}

ANALYZE_SRC = src/cli.c src/diagnostics.c src/indicator_core.c src/indicator.c src/options.c \
              src/indicator_main.c src/indicator_options.c src/indicator_protocol.c

analyze: ## Run gcc -fanalyzer, cppcheck and clang-tidy on the indicator, cli and option sources
	for source in $(ANALYZE_SRC); do \
		$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) -Werror -fanalyzer -c -o /dev/null $$source || exit 1; \
	done
	@if command -v cppcheck > /dev/null; then \
		cppcheck --std=c99 --enable=warning,style,performance,portability --error-exitcode=1 -Isrc $(ANALYZE_SRC); \
	else echo "cppcheck not installed, skipped"; fi
	@if command -v clang-tidy > /dev/null; then \
		clang-tidy $(ANALYZE_SRC) -- $(CPPFLAGS) -std=c99 -Isrc; \
	else echo "clang-tidy not installed, skipped"; fi

TEST_BINS = test/indicator_core_test test/cli_test test/options_test test/indicator_protocol_test

$(TEST_BINS): Makefile

check: $(TEST_BINS) ## Build and run the unit tests (TEST_CFLAGS, WERROR=-Werror)
	./test/indicator_core_test
	./test/cli_test
	./test/options_test
	./test/indicator_protocol_test

test/indicator_core_test: test/indicator_core_test.c src/indicator_core.c src/indicator_core.h src/chain_phase.h src/diagnostics.h
	$(CC) -std=c99 -pedantic -Wall -Wextra $(STRICT_CFLAGS) $(WERROR) $(TEST_CFLAGS) -Isrc -o $@ test/indicator_core_test.c src/indicator_core.c

test/cli_test: test/cli_test.c src/cli.c src/cli.h src/chain_phase.h
	$(CC) -std=c99 -pedantic -Wall -Wextra $(STRICT_CFLAGS) $(WERROR) $(TEST_CFLAGS) -Isrc -o $@ test/cli_test.c src/cli.c

test/options_test: test/options_test.c src/options.c src/options.h src/cli.c src/cli.h src/indicator_core.c src/indicator_core.h src/chain_phase.h src/diagnostics.h src/helpers.h
	$(CC) -std=c99 -pedantic -Wall -Wextra $(STRICT_CFLAGS) $(WERROR) $(TEST_CFLAGS) -Isrc -o $@ test/options_test.c src/options.c src/cli.c src/indicator_core.c

# The core is linked for indicator_derive_banner(): the test checks that the
# banner derived from the replayed view reads as the in-process one.
test/indicator_protocol_test: test/indicator_protocol_test.c src/indicator_protocol.c src/indicator_protocol.h src/indicator_core.c src/indicator_core.h src/chain_phase.h src/diagnostics.h
	$(CC) -std=c99 -pedantic -Wall -Wextra $(STRICT_CFLAGS) $(WERROR) $(TEST_CFLAGS) -Isrc -o $@ test/indicator_protocol_test.c src/indicator_protocol.c src/indicator_core.c

install: ## Install both programs, their man pages and the examples under PREFIX
	mkdir -p "$(DESTDIR)$(BINPREFIX)"
	cp -pf $(OUT) $(INDICATOR_OUT) "$(DESTDIR)$(BINPREFIX)"
	mkdir -p "$(DESTDIR)$(MANPREFIX)"/man1
	cp -p doc/$(OUT).1 doc/$(INDICATOR_OUT).1 "$(DESTDIR)$(MANPREFIX)"/man1
	mkdir -p "$(DESTDIR)$(DOCPREFIX)"
	cp -pr examples "$(DESTDIR)$(DOCPREFIX)"/examples

uninstall: ## Remove what install put under PREFIX
	rm -f "$(DESTDIR)$(BINPREFIX)"/$(OUT) "$(DESTDIR)$(BINPREFIX)"/$(INDICATOR_OUT)
	rm -f "$(DESTDIR)$(MANPREFIX)"/man1/$(OUT).1 "$(DESTDIR)$(MANPREFIX)"/man1/$(INDICATOR_OUT).1
	rm -rf "$(DESTDIR)$(DOCPREFIX)"

doc: ## Regenerate the man pages from doc/*.1.asciidoc (needs a2x)
	a2x -v -d manpage -f manpage -a revnumber=$(VERSION) doc/$(OUT).1.asciidoc
	a2x -v -d manpage -f manpage -a revnumber=$(VERSION) doc/$(INDICATOR_OUT).1.asciidoc

clean: ## Remove the objects, the programs and the test binaries
	rm -f $(sort $(OBJ) $(INDICATOR_OBJ)) $(OUT) $(INDICATOR_OUT) $(TEST_BINS)

.PHONY: all debug install uninstall doc clean check check-indicator-deps analyze help
