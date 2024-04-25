# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that generates gen/pkgs.mk

include config/prelude.mk
include ${GEN}/vars.mk
include ${GEN}/flags.mk
include config/exports.mk

# External dependencies
USE_PKGS := \
    ${GEN}/libacl.use \
    ${GEN}/libcap.use \
    ${GEN}/libselinux.use \
    ${GEN}/liburing.use \
    ${GEN}/oniguruma.use

${GEN}/pkgs.mk: ${USE_PKGS}
	${MSG} "[ GEN] ${TGT}"
	printf '# %s\n' "${TGT}" >$@
	gen() { \
	    printf 'PKGS := %s\n' "$$*"; \
	    printf 'CFLAGS += %s\n' "$$(config/pkgconf.sh --cflags "$$@")"; \
	    printf 'LDFLAGS += %s\n' "$$(config/pkgconf.sh --ldflags "$$@")"; \
	    printf 'LDLIBS := %s $${LDLIBS}\n' "$$(config/pkgconf.sh --ldlibs "$$@")"; \
	}; \
	gen $$(cat ${.ALLSRC}) >>$@
	${VCAT} $@
.PHONY: ${GEN}/pkgs.mk

# Convert ${GEN}/foo.use to foo
PKG = ${@:${GEN}/%.use=%}

${USE_PKGS}::
	if config/pkgconf.sh ${PKG} 2>$@.log; then \
	    printf '%s\n' ${PKG} >$@; \
	    test "${IS_V}" || printf '[ CC ] %-${MSG_WIDTH}s  ✔\n' config/${PKG}.c; \
	else \
	    : >$@; \
	    test "${IS_V}" || printf '[ CC ] %-${MSG_WIDTH}s  ✘\n' config/${PKG}.c; \
	fi
