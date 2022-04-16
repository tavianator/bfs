############################################################################
# bfs                                                                      #
# Copyright (C) 2015-2021 Tavian Barnes <tavianator@tavianator.com>        #
#                                                                          #
# Permission to use, copy, modify, and/or distribute this software for any #
# purpose with or without fee is hereby granted.                           #
#                                                                          #
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES #
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF         #
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR  #
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES   #
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN    #
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF  #
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.           #
############################################################################

ifeq ($(wildcard .git),)
VERSION := 2.5
else
VERSION := $(shell git describe --always)
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

DEFAULT_CFLAGS := \
    -g \
    -Wall \
    -Wmissing-declarations \
    -Wshadow \
    -Wsign-compare \
    -Wstrict-prototypes \
    -Wimplicit-fallthrough

CFLAGS ?= $(DEFAULT_CFLAGS)
LDFLAGS ?=
DEPFLAGS ?= -MD -MP -MF $(@:.o=.d)

DESTDIR ?=
PREFIX ?= /usr
MANDIR ?= $(PREFIX)/share/man

LOCAL_CPPFLAGS := \
    -D__EXTENSIONS__ \
    -D_ATFILE_SOURCE \
    -D_BSD_SOURCE \
    -D_DARWIN_C_SOURCE \
    -D_DEFAULT_SOURCE \
    -D_FILE_OFFSET_BITS=64 \
    -D_TIME_BITS=64 \
    -D_GNU_SOURCE \
    -DBFS_VERSION=\"$(VERSION)\"

LOCAL_CFLAGS := -std=c11
LOCAL_LDFLAGS :=
LOCAL_LDLIBS :=

ASAN := $(filter asan,$(MAKECMDGOALS))
LSAN := $(filter lsan,$(MAKECMDGOALS))
MSAN := $(filter msan,$(MAKECMDGOALS))
TSAN := $(filter tsan,$(MAKECMDGOALS))
UBSAN := $(filter ubsan,$(MAKECMDGOALS))

ifndef MSAN
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
endif

ifeq ($(OS),Linux)
ifndef MSAN # These libraries are not built with msan
WITH_ACL := y
WITH_ATTR := y
WITH_LIBCAP := y
endif

ifdef WITH_ACL
LOCAL_LDLIBS += -lacl
else
LOCAL_CPPFLAGS += -DBFS_HAS_SYS_ACL=0
endif

ifdef WITH_ATTR
LOCAL_LDLIBS += -lattr
else
LOCAL_CPPFLAGS += -DBFS_HAS_SYS_XATTR=0
endif

ifdef WITH_LIBCAP
LOCAL_LDLIBS += -lcap
else
LOCAL_CPPFLAGS += -DBFS_HAS_SYS_CAPABILITY=0
endif

LOCAL_LDFLAGS += -Wl,--as-needed
LOCAL_LDLIBS += -lrt

DISTCHECK_FLAGS := TEST_FLAGS="--verbose --all --sudo"
else # Linux
DISTCHECK_FLAGS := TEST_FLAGS="--verbose"
endif

ifeq ($(OS),NetBSD)
LOCAL_LDLIBS += -lutil
endif

ifdef ASAN
LOCAL_CFLAGS += -fsanitize=address
SANITIZE := y
endif

ifdef LSAN
LOCAL_CFLAGS += -fsanitize=leak
SANITIZE := y
endif

ifdef MSAN
LOCAL_CFLAGS += -fsanitize=memory -fsanitize-memory-track-origins
SANITIZE := y
endif

ifdef TSAN
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

ifneq ($(filter gcov,$(MAKECMDGOALS)),)
LOCAL_CFLAGS += --coverage
endif

ifneq ($(filter release,$(MAKECMDGOALS)),)
CFLAGS := $(DEFAULT_CFLAGS) -O3 -flto -DNDEBUG
endif

ALL_CPPFLAGS = $(LOCAL_CPPFLAGS) $(CPPFLAGS) $(EXTRA_CPPFLAGS)
ALL_CFLAGS = $(ALL_CPPFLAGS) $(LOCAL_CFLAGS) $(CFLAGS) $(EXTRA_CFLAGS) $(DEPFLAGS)
ALL_LDFLAGS = $(ALL_CFLAGS) $(LOCAL_LDFLAGS) $(LDFLAGS) $(EXTRA_LDFLAGS)
ALL_LDLIBS = $(LOCAL_LDLIBS) $(LDLIBS) $(EXTRA_LDLIBS)

# Save the full set of flags to rebuild everything when they change
ALL_FLAGS := $(CC) : $(ALL_CFLAGS) : $(ALL_LDFLAGS) : $(ALL_LDLIBS)
$(shell ./flags.sh $(ALL_FLAGS))

# Goals that make binaries
BIN_GOALS := bfs tests/mksock tests/trie tests/xtimegm

# Goals that are treated like flags by this Makefile
FLAG_GOALS := asan lsan msan tsan ubsan gcov release

# These are the remaining non-flag goals
GOALS := $(filter-out $(FLAG_GOALS),$(MAKECMDGOALS))

# Build the default goal if only flag goals are specified
FLAG_PREREQS :=
ifndef GOALS
FLAG_PREREQS += default
endif

# The different search strategies that we test
STRATEGIES := bfs dfs ids eds
STRATEGY_CHECKS := $(STRATEGIES:%=check-%)

# All the different checks we run
CHECKS := $(STRATEGY_CHECKS) check-trie check-xtimegm

default: bfs

all: $(BIN_GOALS)

bfs: \
    build/bar.o \
    build/bftw.o \
    build/color.o \
    build/ctx.o \
    build/darray.o \
    build/diag.o \
    build/dir.o \
    build/dstring.o \
    build/eval.o \
    build/exec.o \
    build/fsade.o \
    build/main.o \
    build/mtab.o \
    build/opt.o \
    build/parse.o \
    build/printf.o \
    build/pwcache.o \
    build/stat.o \
    build/trie.o \
    build/typo.o \
    build/util.o \
    build/xregex.o \
    build/xspawn.o \
    build/xtime.o

tests/mksock: tests/mksock.o
tests/trie: build/trie.o tests/trie.o
tests/xtimegm: build/xtime.o tests/xtimegm.o

$(BIN_GOALS):
	+$(CC) $(ALL_LDFLAGS) $^ $(ALL_LDLIBS) -o $@

build/%.o: src/%.c .flags
	$(CC) $(ALL_CFLAGS) -c $< -o $@
    
tests/%.o: tests/%.c .flags
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# Need a rule for .flags to convince make to apply the above pattern rule if
# .flags didn't exist when make was run
.flags:

# Make sure that "make release" builds everything, but "make release main.o" doesn't
$(FLAG_GOALS): $(FLAG_PREREQS)
	@:

check: $(CHECKS)

$(STRATEGY_CHECKS): check-%: bfs tests/mksock
	./tests.sh --bfs="./bfs -S $*" $(TEST_FLAGS)

check-trie check-xtimegm: check-%: tests/%
	$<

distcheck:
	+$(MAKE) -B asan ubsan check $(DISTCHECK_FLAGS)
ifneq ($(OS),Darwin)
	+$(MAKE) -B msan check CC=clang $(DISTCHECK_FLAGS)
endif
ifeq ($(OS) $(ARCH),Linux x86_64)
	+$(MAKE) -B check EXTRA_CFLAGS="-m32" ONIG_CONFIG= $(DISTCHECK_FLAGS)
endif
	+$(MAKE) -B release check $(DISTCHECK_FLAGS)
	+$(MAKE) -B check $(DISTCHECK_FLAGS)

clean:
	$(RM) $(BIN_GOALS) .flags *.[od] *.gcda *.gcno tests/*.[od] tests/*.gcda tests/*.gcno

install:
	$(MKDIR) $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -m755 bfs $(DESTDIR)$(PREFIX)/bin/bfs
	$(MKDIR) $(DESTDIR)$(MANDIR)/man1
	$(INSTALL) -m644 bfs.1 $(DESTDIR)$(MANDIR)/man1/bfs.1
	$(MKDIR) $(DESTDIR)$(PREFIX)/share/bash-completion/completions
	$(INSTALL) -m644 completions/bfs.bash $(DESTDIR)$(PREFIX)/share/bash-completion/completions/bfs

uninstall:
	$(RM) $(DESTDIR)$(PREFIX)/share/bash-completion/completions/bfs
	$(RM) $(DESTDIR)$(MANDIR)/man1/bfs.1
	$(RM) $(DESTDIR)$(PREFIX)/bin/bfs

.PHONY: default all $(FLAG_GOALS) check $(CHECKS) distcheck clean install uninstall

.SUFFIXES:

-include $(wildcard *.d)

$(shell mkdir -p build)