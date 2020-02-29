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
VERSION := 1.6
else
VERSION := $(shell git describe --always)
endif

ifndef OS
OS := $(shell uname)
endif

CC ?= gcc
INSTALL ?= install
MKDIR ?= mkdir -p
RM ?= rm -f

WFLAGS ?= -Wall -Wmissing-declarations -Wstrict-prototypes
CFLAGS ?= -g $(WFLAGS)
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
MSAN_CFLAGS := -fsanitize=memory
UBSAN_CFLAGS := -fsanitize=undefined

ifeq ($(OS),Linux)
LOCAL_LDFLAGS += -Wl,--as-needed
LOCAL_LDLIBS += -lacl -lcap -lattr -lrt

# These libraries are not built with msan, so disable them
MSAN_CFLAGS += -DBFS_HAS_SYS_ACL=0 -DBFS_HAS_SYS_CAPABILITY=0 -DBFS_HAS_SYS_XATTR=0
endif

ALL_CPPFLAGS = $(LOCAL_CPPFLAGS) $(CPPFLAGS)
ALL_CFLAGS = $(ALL_CPPFLAGS) $(LOCAL_CFLAGS) $(CFLAGS) $(DEPFLAGS)
ALL_LDFLAGS = $(ALL_CFLAGS) $(LOCAL_LDFLAGS) $(LDFLAGS)
ALL_LDLIBS = $(LOCAL_LDLIBS) $(LDLIBS)

default: bfs

all: bfs tests/mksock

bfs: \
    bftw.o \
    color.o \
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
    passwd.o \
    printf.o \
    spawn.o \
    stat.o \
    time.o \
    trie.o \
    typo.o \
    util.o
	$(CC) $(ALL_LDFLAGS) $^ $(ALL_LDLIBS) -o $@

release: CFLAGS := -g $(WFLAGS) -O3 -flto -DNDEBUG
release: bfs

tests/mksock: tests/mksock.o
	$(CC) $(ALL_LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

check: check-bfs check-dfs check-ids

check-%: all
	./tests.sh --bfs="$(CURDIR)/bfs -S $*"

distcheck:
	+$(MAKE) -Bs check CFLAGS="$(CFLAGS) $(ASAN_CFLAGS) $(UBSAN_CFLAGS)"
ifneq ($(OS),Darwin)
	+$(MAKE) -Bs check CC=clang CFLAGS="$(CFLAGS) $(MSAN_CFLAGS)"
	+$(MAKE) -Bs check CFLAGS="$(CFLAGS) -m32"
endif
	+$(MAKE) -Bs release check
	+$(MAKE) -Bs check
ifeq ($(OS),Linux)
	./tests.sh --sudo --bfs="$(CURDIR)/bfs"
	./tests.sh --sudo --bfs="$(CURDIR)/bfs -S dfs"
	./tests.sh --sudo --bfs="$(CURDIR)/bfs -S ids"
endif

clean:
	$(RM) bfs *.[od] tests/mksock tests/*.[od]

install:
	$(MKDIR) $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -m755 bfs $(DESTDIR)$(PREFIX)/bin/bfs
	$(MKDIR) $(DESTDIR)$(MANDIR)/man1
	$(INSTALL) -m644 bfs.1 $(DESTDIR)$(MANDIR)/man1/bfs.1

uninstall:
	$(RM) $(DESTDIR)$(PREFIX)/bin/bfs
	$(RM) $(DESTDIR)$(MANDIR)/man1/bfs.1

.PHONY: all release check distcheck clean install uninstall

-include $(wildcard *.d)
