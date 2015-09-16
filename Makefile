#####################################################################
# bfs                                                               #
# Copyright (C) 2015 Tavian Barnes <tavianator@tavianator.com>      #
#                                                                   #
# This program is free software. It comes without any warranty, to  #
# the extent permitted by applicable law. You can redistribute it   #
# and/or modify it under the terms of the Do What The Fuck You Want #
# To Public License, Version 2, as published by Sam Hocevar. See    #
# the COPYING file or http://www.wtfpl.net/ for more details.       #
#####################################################################

CC ?= gcc
CFLAGS ?= -g -Wall
LDFLAGS ?=
DEPFLAGS ?= -MD -MP -MF $(@:.o=.d)
RM ?= rm -f

LOCAL_CPPFLAGS := -D_DEFAULT_SOURCE
LOCAL_CFLAGS := -std=c99

ALL_CPPFLAGS = $(LOCAL_CPPFLAGS) $(CPPFLAGS)
ALL_CFLAGS = $(ALL_CPPFLAGS) $(LOCAL_CFLAGS) $(CFLAGS) $(DEPFLAGS)
ALL_LDFLAGS = $(ALL_CFLAGS) $(LDFLAGS)

all: bfs

bfs: bfs.o bftw.o color.o
	$(CC) $(ALL_LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

check: all
	./tests.sh

clean:
	$(RM) bfs *.o *.d

release: CFLAGS := -O3 -flto -Wall -DNDEBUG
release: bfs

.PHONY: all check clean release

-include $(wildcard *.d)
