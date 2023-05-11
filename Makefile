# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

ifneq ($(wildcard .git),)
VERSION := $(shell git describe --always 2>/dev/null)
endif

ifndef VERSION
VERSION := 2.6.3
endif

ifndef OS
OS := $(shell uname)
endif

ifndef ARCH
ARCH := $(shell uname -m)
endif

CC ?= gcc
INSTALL ?= install
MKDIR ?= mkdir -p
RM ?= rm -f

export BUILDDIR ?= .
DESTDIR ?=
PREFIX ?= /usr
MANDIR ?= $(PREFIX)/share/man

BIN := $(BUILDDIR)/bin
OBJ := $(BUILDDIR)/obj

DEFAULT_CFLAGS := \
    -g \
    -Wall \
    -Wformat=2 \
    -Werror=implicit \
    -Wimplicit-fallthrough \
    -Wmissing-declarations \
    -Wshadow \
    -Wsign-compare \
    -Wstrict-prototypes

CFLAGS ?= $(DEFAULT_CFLAGS)
LDFLAGS ?=
DEPFLAGS ?= -MD -MP -MF $(@:.o=.d)

LOCAL_CPPFLAGS := \
    -D__EXTENSIONS__ \
    -D_ATFILE_SOURCE \
    -D_BSD_SOURCE \
    -D_DARWIN_C_SOURCE \
    -D_DEFAULT_SOURCE \
    -D_GNU_SOURCE \
    -D_LARGEFILE64_SOURCE \
    -D_FILE_OFFSET_BITS=64 \
    -D_TIME_BITS=64 \
    -DBFS_VERSION=\"$(VERSION)\"

LOCAL_CFLAGS := -std=c17
LOCAL_LDFLAGS :=
LOCAL_LDLIBS :=

ASAN := $(filter asan,$(MAKECMDGOALS))
LSAN := $(filter lsan,$(MAKECMDGOALS))
MSAN := $(filter msan,$(MAKECMDGOALS))
TSAN := $(filter tsan,$(MAKECMDGOALS))
UBSAN := $(filter ubsan,$(MAKECMDGOALS))

ifdef ASAN
LOCAL_CFLAGS += -fsanitize=address
SANITIZE := y
endif

ifdef LSAN
LOCAL_CFLAGS += -fsanitize=leak
SANITIZE := y
endif

ifdef MSAN
# msan needs all code instrumented
NOLIBS := y
LOCAL_CFLAGS += -fsanitize=memory -fsanitize-memory-track-origins
SANITIZE := y
endif

ifdef TSAN
# tsan needs all code instrumented
NOLIBS := y
# https://github.com/google/sanitizers/issues/342
LOCAL_CPPFLAGS += -DBFS_TARGET_CLONES=false
LOCAL_CFLAGS += -fsanitize=thread
SANITIZE := y
endif

ifdef UBSAN
LOCAL_CFLAGS += -fsanitize=undefined
SANITIZE := y
endif

ifdef SANITIZE
LOCAL_CFLAGS += -fno-sanitize-recover=all
endif

ifndef NOLIBS
WITH_ONIGURUMA := y
endif

ifdef WITH_ONIGURUMA
LOCAL_CPPFLAGS += -DBFS_WITH_ONIGURUMA=1

ONIG_CONFIG := $(shell command -v onig-config 2>/dev/null)
ifdef ONIG_CONFIG
ONIG_CFLAGS := $(shell $(ONIG_CONFIG) --cflags)
ONIG_LDLIBS := $(shell $(ONIG_CONFIG) --libs)
else
ONIG_LDLIBS := -lonig
endif

LOCAL_CFLAGS += $(ONIG_CFLAGS)
LOCAL_LDLIBS += $(ONIG_LDLIBS)
endif # WITH_ONIGURUMA

ifeq ($(OS),Linux)
ifndef NOLIBS
WITH_ACL := y
WITH_ATTR := y
WITH_LIBCAP := y
endif

ifdef WITH_ACL
LOCAL_LDLIBS += -lacl
else
LOCAL_CPPFLAGS += -DBFS_USE_SYS_ACL_H=0
endif

ifdef WITH_ATTR
LOCAL_LDLIBS += -lattr
else
LOCAL_CPPFLAGS += -DBFS_USE_SYS_XATTR_H=0
endif

ifdef WITH_LIBCAP
LOCAL_LDLIBS += -lcap
else
LOCAL_CPPFLAGS += -DBFS_USE_SYS_CAPABILITY_H=0
endif

LOCAL_LDFLAGS += -Wl,--as-needed
LOCAL_LDLIBS += -lrt
endif # Linux

ifeq ($(OS),NetBSD)
LOCAL_LDLIBS += -lutil
endif

ifneq ($(filter gcov,$(MAKECMDGOALS)),)
LOCAL_CFLAGS += --coverage
# gcov only intercepts fork()/exec() with -std=gnu*
LOCAL_CFLAGS := $(patsubst -std=c%,-std=gnu%,$(LOCAL_CFLAGS))
endif

ifneq ($(filter release,$(MAKECMDGOALS)),)
CFLAGS := $(DEFAULT_CFLAGS) -O3 -flto -DNDEBUG
endif

ALL_CPPFLAGS = $(LOCAL_CPPFLAGS) $(CPPFLAGS) $(EXTRA_CPPFLAGS)
ALL_CFLAGS = $(ALL_CPPFLAGS) $(LOCAL_CFLAGS) $(CFLAGS) $(EXTRA_CFLAGS) $(DEPFLAGS)
ALL_LDFLAGS = $(ALL_CFLAGS) $(LOCAL_LDFLAGS) $(LDFLAGS) $(EXTRA_LDFLAGS)
ALL_LDLIBS = $(LOCAL_LDLIBS) $(LDLIBS) $(EXTRA_LDLIBS)

# Default make target
bfs: $(BIN)/bfs
.PHONY: bfs

# Goals that are treated like flags by this Makefile
FLAG_GOALS := asan lsan msan tsan ubsan gcov release

# These are the remaining non-flag goals
GOALS := $(filter-out $(FLAG_GOALS),$(MAKECMDGOALS))

# Build the default goal if only flag goals are specified
FLAG_PREREQS :=
ifndef GOALS
FLAG_PREREQS += bfs
endif

# Make sure that "make release" builds everything, but "make release obj/src/main.o" doesn't
$(FLAG_GOALS): $(FLAG_PREREQS)
	@:
.PHONY: $(FLAG_GOALS)

all: bfs tests
.PHONY: all

$(BIN)/%:
	@$(MKDIR) $(@D)
	+$(CC) $(ALL_LDFLAGS) $^ $(ALL_LDLIBS) -o $@
ifeq ($(OS) $(TSAN),FreeBSD tsan)
	elfctl -e +noaslr $@
endif

$(OBJ)/%.o: %.c $(OBJ)/FLAGS
	@$(MKDIR) $(@D)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# Save the full set of flags to rebuild everything when they change
$(OBJ)/FLAGS.new:
	@$(MKDIR) $(@D)
	@echo $(CC) : $(ALL_CFLAGS) : $(ALL_LDFLAGS) : $(ALL_LDLIBS) >$@
.PHONY: $(OBJ)/FLAGS.new

# Only update obj/FLAGS if obj/FLAGS.new is different
$(OBJ)/FLAGS: $(OBJ)/FLAGS.new
	@test -e $@ && cmp -s $@ $< && rm $< || mv $< $@

# All object files except the entry point
LIBBFS := \
    $(OBJ)/src/bar.o \
    $(OBJ)/src/bfstd.o \
    $(OBJ)/src/bftw.o \
    $(OBJ)/src/color.o \
    $(OBJ)/src/ctx.o \
    $(OBJ)/src/darray.o \
    $(OBJ)/src/diag.o \
    $(OBJ)/src/dir.o \
    $(OBJ)/src/dstring.o \
    $(OBJ)/src/eval.o \
    $(OBJ)/src/exec.o \
    $(OBJ)/src/fsade.o \
    $(OBJ)/src/mtab.o \
    $(OBJ)/src/opt.o \
    $(OBJ)/src/parse.o \
    $(OBJ)/src/printf.o \
    $(OBJ)/src/pwcache.o \
    $(OBJ)/src/stat.o \
    $(OBJ)/src/trie.o \
    $(OBJ)/src/typo.o \
    $(OBJ)/src/xregex.o \
    $(OBJ)/src/xspawn.o \
    $(OBJ)/src/xtime.o

# The main executable
$(BIN)/bfs: $(OBJ)/src/main.o $(LIBBFS)

# Standalone unit tests
UNITS := bfstd bit trie xtimegm
UNIT_TESTS := $(UNITS:%=$(BIN)/tests/%)
UNIT_CHECKS := $(UNITS:%=check-%)

# Testing utilities
TEST_UTILS := $(BIN)/tests/mksock $(BIN)/tests/xtouch

TESTS := $(UNIT_TESTS) $(TEST_UTILS)

tests: $(TESTS)
.PHONY: tests

$(TESTS): $(BIN)/tests/%: $(OBJ)/tests/%.o $(LIBBFS)

# The different search strategies that we test
STRATEGIES := bfs dfs ids eds
STRATEGY_CHECKS := $(STRATEGIES:%=check-%)

# All the different checks we run
CHECKS := $(UNIT_CHECKS) $(STRATEGY_CHECKS)

check: $(CHECKS)
.PHONY: check $(CHECKS)

$(UNIT_CHECKS): check-%: $(BIN)/tests/%
	$<

$(STRATEGY_CHECKS): check-%: $(BIN)/bfs $(TEST_UTILS)
	./tests/tests.sh --bfs="$(BIN)/bfs -S $*" $(TEST_FLAGS)

# Custom test flags for distcheck
DISTCHECK_FLAGS := -s TEST_FLAGS="--sudo --verbose=skipped"

distcheck:
	+$(MAKE) -B asan ubsan check $(DISTCHECK_FLAGS)
ifneq ($(OS),Darwin)
	+$(MAKE) -B msan ubsan check CC=clang $(DISTCHECK_FLAGS)
endif
	+$(MAKE) -B tsan ubsan check CC=clang $(DISTCHECK_FLAGS)
ifeq ($(OS) $(ARCH),Linux x86_64)
	+$(MAKE) -B check EXTRA_CFLAGS="-m32" ONIG_CONFIG= $(DISTCHECK_FLAGS)
endif
	+$(MAKE) -B release check $(DISTCHECK_FLAGS)
	+$(MAKE) -B check $(DISTCHECK_FLAGS)
	+$(MAKE) check-install $(DISTCHECK_FLAGS)
.PHONY: distcheck

clean:
	$(RM) -r $(BIN) $(OBJ)
.PHONY: clean

install:
	$(MKDIR) $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -m755 $(BIN)/bfs $(DESTDIR)$(PREFIX)/bin/bfs
	$(MKDIR) $(DESTDIR)$(MANDIR)/man1
	$(INSTALL) -m644 docs/bfs.1 $(DESTDIR)$(MANDIR)/man1/bfs.1
	$(MKDIR) $(DESTDIR)$(PREFIX)/share/bash-completion/completions
	$(INSTALL) -m644 completions/bfs.bash $(DESTDIR)$(PREFIX)/share/bash-completion/completions/bfs
	$(MKDIR) $(DESTDIR)$(PREFIX)/share/zsh/site-functions
	$(INSTALL) -m644 completions/bfs.zsh $(DESTDIR)$(PREFIX)/share/zsh/site-functions/_bfs
	$(MKDIR) $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d
	$(INSTALL) -m644 completions/bfs.fish $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/bfs.fish
.PHONY: install

uninstall:
	$(RM) $(DESTDIR)$(PREFIX)/share/bash-completion/completions/bfs
	$(RM) $(DESTDIR)$(PREFIX)/share/zsh/site-functions/_bfs
	$(RM) $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/bfs.fish
	$(RM) $(DESTDIR)$(MANDIR)/man1/bfs.1
	$(RM) $(DESTDIR)$(PREFIX)/bin/bfs
.PHONY: uninstall

check-install:
	+$(MAKE) install DESTDIR=$(BUILDDIR)/pkg
	+$(MAKE) uninstall DESTDIR=$(BUILDDIR)/pkg
	$(BIN)/bfs $(BUILDDIR)/pkg -not -type d -print -exit 1
	$(RM) -r $(BUILDDIR)/pkg
.PHONY: check-install

.SUFFIXES:

-include $(wildcard $(OBJ)/*/*.d)
