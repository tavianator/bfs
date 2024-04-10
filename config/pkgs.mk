# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that generates gen/pkgs.mk

.OBJDIR: .

include config/vars.mk
include ${GEN}/pkgs.mk

default::
	config/pkgconf.sh --cflags ${PKGS} >>${TARGET} 2>>${TARGET}.log
	config/pkgconf.sh --ldflags ${PKGS} >>${TARGET} 2>>${TARGET}.log
	config/pkgconf.sh --ldlibs ${PKGS} >>${TARGET} 2>>${TARGET}.log
