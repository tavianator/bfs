# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that generates gen/deps.mk

.OBJDIR: .

include config/vars.mk

default::
	if config/cc.sh -MD -MP -MF /dev/null config/empty.c; then \
	    printf 'DEPFLAGS = -MD -MP -MF $${@:.o=.d}\n'; \
	fi >${TARGET} 2>${TARGET}.log
