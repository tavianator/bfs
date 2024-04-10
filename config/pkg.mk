# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that generates gen/lib*.mk

.OBJDIR: .

include config/vars.mk

default::
	config/pkg.sh ${TARGET:${GEN}/%.mk=%} >${TARGET} 2>${TARGET}.log
