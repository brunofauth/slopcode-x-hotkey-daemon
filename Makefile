OUT      = sxhkd
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

CPPFLAGS += -D_POSIX_C_SOURCE=200112L -DVERSION=\"$(VERSION)\" $(INDICATOR_CFLAGS)
CFLAGS   += -std=c99 -pedantic -Wall -Wextra
LDFLAGS  ?=

# Stricter diagnostics, applied to the chain indicator objects and to the
# analyze/check targets only, so that the historic sources keep building with
# the default flags on every compiler.
STRICT_CFLAGS = -Wshadow -Wconversion -Wsign-conversion -Wstrict-prototypes -Wmissing-prototypes \
                -Wold-style-definition -Wvla -Wswitch-enum -Wcast-qual -Wundef -Wdouble-promotion \
                -Wformat=2 -Wnull-dereference -Wimplicit-fallthrough
TEST_CFLAGS  ?= -fsanitize=address,undefined
LDLIBS    = $(LDFLAGS) -lxcb -lxcb-keysyms -lxcb-xkb -lxcb-shape $(INDICATOR_LIBS)

PREFIX    ?= /usr/local
BINPREFIX ?= $(PREFIX)/bin
MANPREFIX ?= $(PREFIX)/share/man
DOCPREFIX ?= $(PREFIX)/share/doc/$(OUT)

all: $(OUT)

debug: CFLAGS += -O0 -g
debug: CPPFLAGS += -DDEBUG
debug: $(OUT)

VPATH = src
OBJ   =

include Sourcedeps

$(OBJ): Makefile | check-indicator-deps

$(OUT): $(OBJ)

indicator_core.o indicator.o: CFLAGS += $(STRICT_CFLAGS)

check-indicator-deps:
	@$(PKG_CONFIG) --exists $(INDICATOR_PKGS) || { \
		echo "error: building sxhkd needs $(PKG_CONFIG) and the development files of: $(INDICATOR_PKGS)" >&2; \
		exit 1; \
	}

ANALYZE_SRC = src/indicator_core.c src/indicator.c

analyze:
	for source in $(ANALYZE_SRC); do \
		$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) -Werror -fanalyzer -c -o /dev/null $$source || exit 1; \
	done
	@if command -v cppcheck > /dev/null; then \
		cppcheck --std=c99 --enable=warning,style,performance,portability --error-exitcode=1 -Isrc $(ANALYZE_SRC); \
	else echo "cppcheck not installed, skipped"; fi
	@if command -v clang-tidy > /dev/null; then \
		clang-tidy $(ANALYZE_SRC) -- $(CPPFLAGS) -std=c99 -Isrc; \
	else echo "clang-tidy not installed, skipped"; fi

TEST_BIN = test/indicator_core_test

check: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): test/indicator_core_test.c src/indicator_core.c src/indicator_core.h src/chain_phase.h src/helpers.h
	$(CC) -std=c99 -pedantic -Wall -Wextra $(STRICT_CFLAGS) -Werror $(TEST_CFLAGS) -Isrc -o $@ test/indicator_core_test.c src/indicator_core.c

install:
	mkdir -p "$(DESTDIR)$(BINPREFIX)"
	cp -pf $(OUT) "$(DESTDIR)$(BINPREFIX)"
	mkdir -p "$(DESTDIR)$(MANPREFIX)"/man1
	cp -p doc/$(OUT).1 "$(DESTDIR)$(MANPREFIX)"/man1
	mkdir -p "$(DESTDIR)$(DOCPREFIX)"
	cp -pr examples "$(DESTDIR)$(DOCPREFIX)"/examples

uninstall:
	rm -f "$(DESTDIR)$(BINPREFIX)"/$(OUT)
	rm -f "$(DESTDIR)$(MANPREFIX)"/man1/$(OUT).1
	rm -rf "$(DESTDIR)$(DOCPREFIX)"

doc:
	a2x -v -d manpage -f manpage -a revnumber=$(VERSION) doc/$(OUT).1.asciidoc

clean:
	rm -f $(OBJ) $(OUT) $(TEST_BIN)

.PHONY: all debug install uninstall doc clean check check-indicator-deps analyze
