OUT      = sxhkd
VERCMD  ?= git describe --tags 2> /dev/null
VERSION := $(shell $(VERCMD) || cat VERSION)

CPPFLAGS += -D_POSIX_C_SOURCE=200112L -DVERSION=\"$(VERSION)\"
CFLAGS   += -std=c99 -pedantic -Wall -Wextra
LDFLAGS  ?=

# Stricter diagnostics, applied to the chain indicator objects and to the
# analyze/check targets only, so that the historic sources keep building with
# the default flags on every compiler.
STRICT_CFLAGS = -Wshadow -Wconversion -Wsign-conversion -Wstrict-prototypes -Wmissing-prototypes \
                -Wold-style-definition -Wvla -Wswitch-enum -Wcast-qual -Wundef -Wdouble-promotion \
                -Wformat=2 -Wnull-dereference -Wimplicit-fallthrough
TEST_CFLAGS  ?= -fsanitize=address,undefined
LDLIBS    = $(LDFLAGS) -lxcb -lxcb-keysyms -lxcb-xkb

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

$(OBJ): Makefile

$(OUT): $(OBJ)

indicator_core.o: CFLAGS += $(STRICT_CFLAGS)

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

.PHONY: all debug install uninstall doc clean check
