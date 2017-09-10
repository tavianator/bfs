############################################################################
# bfs                                                                      #
# Copyright (C) 2015-2017 Tavian Barnes <tavianator@tavianator.com>        #
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
VERSION := 1.1.2
else
VERSION := $(shell git describe --always)
endif

CC ?= gcc
WFLAGS ?= -Wall -Wmissing-declarations
CFLAGS ?= -g $(WFLAGS)
LDFLAGS ?=
DEPFLAGS ?= -MD -MP -MF $(@:.o=.d)
RM ?= rm -f
INSTALL ?= install

DESTDIR ?=
PREFIX ?= /usr

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

ALL_CPPFLAGS = $(LOCAL_CPPFLAGS) $(CPPFLAGS)
ALL_CFLAGS = $(ALL_CPPFLAGS) $(LOCAL_CFLAGS) $(CFLAGS) $(DEPFLAGS)
ALL_LDFLAGS = $(ALL_CFLAGS) $(LDFLAGS)

all: bfs

bfs: bftw.o color.o dstring.o eval.o exec.o main.o mtab.o parse.o printf.o typo.o util.o
	$(CC) $(ALL_LDFLAGS) $^ -o $@

release: CFLAGS := -O3 -flto $(WFLAGS) -DNDEBUG -g
release: bfs

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

check: all
	./tests.sh

clean:
	$(RM) bfs *.o *.d

install:
	$(INSTALL) -D -m755 bfs $(DESTDIR)$(PREFIX)/bin/bfs

uninstall:
	$(RM) $(DESTDIR)$(PREFIX)/bin/bfs

.PHONY: all release check clean install uninstall

-include $(wildcard *.d)
