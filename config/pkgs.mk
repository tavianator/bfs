# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that generates gen/pkgs.mk

include config/prelude.mk
include ${GEN}/vars.mk
include ${GEN}/flags.mk
include ${GEN}/pkgs.mk
include config/exports.mk

${GEN}/pkgs.mk::
	${MSG} "[ GEN] $@"
	config/pkgconf.sh --cflags ${PKGS} >>$@ 2>>$@.log
	config/pkgconf.sh --ldflags ${PKGS} >>$@ 2>>$@.log
	config/pkgconf.sh --ldlibs ${PKGS} >>$@ 2>>$@.log
	${VCAT} $@
