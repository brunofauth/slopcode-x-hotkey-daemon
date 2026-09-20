# The two programs of this package. Each has its own object list, link line
# and man page; `make PROGRAMS=sxhkd` (or `PROGRAMS=sxhkd-indicator`) builds,
# installs or uninstalls just one of them.
PROGRAMS = sxhkd sxhkd-indicator
VERCMD  ?= git describe --tags 2> /dev/null
VERSION := $(shell $(VERCMD) || cat VERSION)

# The on-screen chain indicator draws with cairo and pango; the daemon links
# nothing but xcb, so it builds and installs without pkg-config.
PKG_CONFIG       ?= pkg-config
INDICATOR_PKGS    = cairo cairo-xcb pango pangocairo
# Recursively expanded on purpose: pkg-config only runs when a compile or link
# command of sxhkd-indicator needs it, so every other target (the daemon,
# `check`, `clean`) works without it.
# -isystem keeps the strict warnings from firing inside third-party headers.
INDICATOR_CFLAGS  = $(patsubst -I%,-isystem %,$(shell $(PKG_CONFIG) --cflags $(INDICATOR_PKGS)))
INDICATOR_LIBS    = $(shell $(PKG_CONFIG) --libs $(INDICATOR_PKGS))

# `override` keeps the mandatory flags when CFLAGS/CPPFLAGS are given on the
# make command line (as packagers do), not only through the environment.
override CPPFLAGS += -D_POSIX_C_SOURCE=200112L -DVERSION=\"$(VERSION)\"
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
# The daemon grabs keys and uses XKB; the indicator program does neither and
# only needs the banner's libraries.
LDLIBS           = -lxcb -lxcb-keysyms -lxcb-xkb
INDICATOR_LDLIBS = -lxcb -lxcb-shape $(INDICATOR_LIBS)

PREFIX    ?= /usr/local
BINPREFIX ?= $(PREFIX)/bin
MANPREFIX ?= $(PREFIX)/share/man
DOCPREFIX ?= $(PREFIX)/share/doc/sxhkd

all: $(PROGRAMS) ## Build the programs in PROGRAMS: sxhkd and sxhkd-indicator by default

# Every target followed by "## text" is listed by `make help`: the recipe
# lines below must start with a tab, as in any other rule.
help: ## Show this help message
	@echo "Available targets:"
	@grep -hE '^[a-zA-Z_-]+:.*## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*## "}; {printf "\033[36m%-20s\033[0m %s\n", $$1, $$2}'

debug: override CFLAGS += -O0 -g
debug: override CPPFLAGS += -DDEBUG
debug: $(PROGRAMS) ## Build the programs in PROGRAMS unoptimised with debugging symbols and DEBUG output

VPATH = src
# Sourcedeps assigns every object to the program(s) it belongs to; the
# objects shared by both (the diagnostics, the option engine) are listed
# under each and compiled once.
OBJ           =
INDICATOR_OBJ =

include Sourcedeps

$(sort $(OBJ) $(INDICATOR_OBJ)): Makefile

sxhkd: $(OBJ)

# No built-in rule links a program whose name matches no object (make's is
# `%: %.o`), so unlike sxhkd this one spells out the built-in recipe.
sxhkd-indicator: $(INDICATOR_OBJ) | check-indicator-deps
	$(CC) $(LDFLAGS) $^ $(LOADLIBES) $(INDICATOR_LDLIBS) -o $@

cli.o diagnostics.o indicator_core.o indicator.o options.o indicator_main.o indicator_options.o indicator_protocol.o: override CFLAGS += $(STRICT_CFLAGS)

# Only indicator.c includes the cairo and pango headers, so only its object
# gets the pkg-config include flags (and only the objects that belong to the
# indicator alone are gated on the libraries being installed): compiling the
# daemon never runs pkg-config.
indicator.o: override CPPFLAGS += $(INDICATOR_CFLAGS)
$(filter-out $(OBJ),$(INDICATOR_OBJ)): | check-indicator-deps

check-indicator-deps:
	@$(PKG_CONFIG) --exists $(INDICATOR_PKGS) || { \
		echo "error: building sxhkd-indicator needs $(PKG_CONFIG) and the development files of: $(INDICATOR_PKGS)" >&2; \
		exit 1; \
	}

ANALYZE_SRC = src/cli.c src/diagnostics.c src/indicator_core.c src/indicator.c src/options.c \
              src/indicator_main.c src/indicator_options.c src/indicator_protocol.c

# indicator.c is among the sources, so the analysis needs the indicator's
# headers.
analyze: override CPPFLAGS += $(INDICATOR_CFLAGS)
analyze: check-indicator-deps ## Run gcc -fanalyzer, cppcheck and clang-tidy on the indicator, cli and option sources
	for source in $(ANALYZE_SRC); do \
		$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) -Werror -fanalyzer -c -o /dev/null $$source || exit 1; \
	done
	@if command -v cppcheck > /dev/null; then \
		cppcheck --std=c99 --enable=warning,style,performance,portability --error-exitcode=1 -Isrc $(ANALYZE_SRC); \
	else echo "cppcheck not installed, skipped"; fi
	@if command -v clang-tidy > /dev/null; then \
		clang-tidy $(ANALYZE_SRC) -- $(CPPFLAGS) -std=c99 -Isrc; \
	else echo "clang-tidy not installed, skipped"; fi

# Every test is headless and free of X and of the drawing libraries.
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

test/options_test: test/options_test.c src/options.c src/options.h src/cli.c src/cli.h src/chain_phase.h src/diagnostics.h src/helpers.h
	$(CC) -std=c99 -pedantic -Wall -Wextra $(STRICT_CFLAGS) $(WERROR) $(TEST_CFLAGS) -Isrc -o $@ test/options_test.c src/options.c src/cli.c

# The core is linked for indicator_derive_banner(): the test checks that the
# banner derived from the replayed view reads as the one the daemon reports.
test/indicator_protocol_test: test/indicator_protocol_test.c src/indicator_protocol.c src/indicator_protocol.h src/indicator_core.c src/indicator_core.h src/chain_phase.h src/diagnostics.h
	$(CC) -std=c99 -pedantic -Wall -Wextra $(STRICT_CFLAGS) $(WERROR) $(TEST_CFLAGS) -Isrc -o $@ test/indicator_protocol_test.c src/indicator_protocol.c src/indicator_core.c

# The examples belong to the daemon: they go in with sxhkd only, so that the
# two programs packaged separately never both install them.
install: ## Install the programs in PROGRAMS with their man pages, and the examples with sxhkd, under PREFIX
	mkdir -p "$(DESTDIR)$(BINPREFIX)" "$(DESTDIR)$(MANPREFIX)"/man1
	for program in $(PROGRAMS); do \
		cp -pf $$program "$(DESTDIR)$(BINPREFIX)" && cp -p doc/$$program.1 "$(DESTDIR)$(MANPREFIX)"/man1 || exit 1; \
	done
ifneq ($(filter sxhkd,$(PROGRAMS)),)
	mkdir -p "$(DESTDIR)$(DOCPREFIX)"
	cp -pr examples "$(DESTDIR)$(DOCPREFIX)"/examples
endif

uninstall: ## Remove what install put under PREFIX for the programs in PROGRAMS
	for program in $(PROGRAMS); do \
		rm -f "$(DESTDIR)$(BINPREFIX)"/$$program "$(DESTDIR)$(MANPREFIX)"/man1/$$program.1; \
	done
ifneq ($(filter sxhkd,$(PROGRAMS)),)
	rm -rf "$(DESTDIR)$(DOCPREFIX)"
endif

doc: ## Regenerate the man pages from doc/*.1.asciidoc (needs a2x)
	for program in $(PROGRAMS); do \
		a2x -v -d manpage -f manpage -a revnumber=$(VERSION) doc/$$program.1.asciidoc || exit 1; \
	done

clean: ## Remove the objects, both programs and the test binaries
	rm -f $(sort $(OBJ) $(INDICATOR_OBJ)) sxhkd sxhkd-indicator $(TEST_BINS)

.PHONY: all debug install uninstall doc clean check check-indicator-deps analyze help
