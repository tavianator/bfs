############################################################################
# bfs                                                                      #
# Copyright (C) 2015-2020 Tavian Barnes <tavianator@tavianator.com>        #
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
VERSION := 2.1
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

DEFAULT_CFLAGS ?= -g -Wall -Wmissing-declarations -Wstrict-prototypes -Wsign-compare

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
    -D_GNU_SOURCE \
    -DBFS_VERSION=\"$(VERSION)\"

LOCAL_CFLAGS := -std=c99
LOCAL_LDFLAGS :=
LOCAL_LDLIBS :=

ASAN_CFLAGS := -fsanitize=address
MSAN_CFLAGS := -fsanitize=memory -fsanitize-memory-track-origins
UBSAN_CFLAGS := -fsanitize=undefined

ifeq ($(OS),Linux)
LOCAL_LDFLAGS += -Wl,--as-needed
LOCAL_LDLIBS += -lacl -lcap -lattr -lrt

# These libraries are not built with msan, so disable them
MSAN_CFLAGS += -DBFS_HAS_SYS_ACL=0 -DBFS_HAS_SYS_CAPABILITY=0 -DBFS_HAS_SYS_XATTR=0

DISTCHECK_FLAGS := TEST_FLAGS="--verbose --all --sudo"
else
DISTCHECK_FLAGS := TEST_FLAGS="--verbose"
endif

ifneq ($(filter asan,$(MAKECMDGOALS)),)
LOCAL_CFLAGS += $(ASAN_CFLAGS)
SANITIZE := y
endif

ifneq ($(filter msan,$(MAKECMDGOALS)),)
LOCAL_CFLAGS += $(MSAN_CFLAGS)
SANITIZE := y
endif

ifneq ($(filter ubsan,$(MAKECMDGOALS)),)
LOCAL_CFLAGS += $(UBSAN_CFLAGS)
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

ALL_CPPFLAGS = $(LOCAL_CPPFLAGS) $(CPPFLAGS)
ALL_CFLAGS = $(ALL_CPPFLAGS) $(LOCAL_CFLAGS) $(CFLAGS) $(DEPFLAGS)
ALL_LDFLAGS = $(ALL_CFLAGS) $(LOCAL_LDFLAGS) $(LDFLAGS)
ALL_LDLIBS = $(LOCAL_LDLIBS) $(LDLIBS)

# Save the full set of flags to rebuild everything when they change
ALL_FLAGS := $(CC) : $(ALL_CFLAGS) : $(ALL_LDFLAGS) : $(ALL_LDLIBS)
$(shell ./flags.sh $(ALL_FLAGS))

default: bfs

all: bfs tests/mksock tests/trie tests/xtimegm

bfs: \
    bar.o \
    bftw.o \
    color.o \
    ctx.o \
    darray.o \
    diag.o \
    dstring.o \
    eval.o \
    exec.o \
    fsade.o \
    main.o \
    mtab.o \
    opt.o \
    parse.o \
    printf.o \
    pwcache.o \
    spawn.o \
    stat.o \
    time.o \
    trie.o \
    typo.o \
    util.o
	$(CC) $(ALL_LDFLAGS) $^ $(ALL_LDLIBS) -o $@

asan: bfs
	@:
ubsan: bfs
	@:
msan: bfs
	@:
gcov: bfs
	@:
release: bfs
	@:

tests/mksock: tests/mksock.o
	$(CC) $(ALL_LDFLAGS) $^ -o $@

tests/trie: trie.o tests/trie.o
	$(CC) $(ALL_LDFLAGS) $^ -o $@

tests/xtimegm: time.o tests/xtimegm.o
	$(CC) $(ALL_LDFLAGS) $^ -o $@

%.o: %.c .flags
	$(CC) $(ALL_CFLAGS) -c $< -o $@

check: check-trie check-xtimegm check-bfs check-dfs check-ids check-eds

check-trie: tests/trie
	$<

check-xtimegm: tests/xtimegm
	$<

check-%: all
	./tests.sh --bfs="$(CURDIR)/bfs -S $*" $(TEST_FLAGS)

distcheck:
	+$(MAKE) -B asan ubsan check $(DISTCHECK_FLAGS)
ifneq ($(OS),Darwin)
	+$(MAKE) -B msan check CC=clang $(DISTCHECK_FLAGS)
ifeq ($(ARCH),x86_64)
	+$(MAKE) -B check CFLAGS="-m32" $(DISTCHECK_FLAGS)
endif
endif
	+$(MAKE) -B release check $(DISTCHECK_FLAGS)
	+$(MAKE) -B check $(DISTCHECK_FLAGS)

clean:
	$(RM) bfs *.[od] *.gcda *.gcno tests/mksock tests/trie tests/xtimegm tests/*.[od] tests/*.gcda tests/*.gcno

install:
	$(MKDIR) $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -m755 bfs $(DESTDIR)$(PREFIX)/bin/bfs
	$(MKDIR) $(DESTDIR)$(MANDIR)/man1
	$(INSTALL) -m644 bfs.1 $(DESTDIR)$(MANDIR)/man1/bfs.1

uninstall:
	$(RM) $(DESTDIR)$(PREFIX)/bin/bfs
	$(RM) $(DESTDIR)$(MANDIR)/man1/bfs.1

.PHONY: all asan ubsan msan release check check-trie check-xtimegm distcheck clean install uninstall

-include $(wildcard *.d)
