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
CFLAGS ?= -std=c99 -g -Og -Wall -D_DEFAULT_SOURCE
LDFLAGS ?=
RM ?= rm -f

DEPS := bftw.h

bfs: bfs.o bftw.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) bfs *.o

.PHONY: clean
