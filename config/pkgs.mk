# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that generates gen/pkgs.mk

include config/prelude.mk
include ${GEN}/vars.mk
include ${GEN}/flags.mk
include config/exports.mk

HEADERS := ${ALL_PKGS:%=${GEN}/use/%.h}

${GEN}/pkgs.mk: ${HEADERS}
	${MSG} "[ GEN] ${TGT}"
	printf '# %s\n' "${TGT}" >$@
	gen() { \
	    printf 'PKGS := %s\n' "$$*"; \
	    printf 'CFLAGS += %s\n' "$$(config/pkgconf.sh --cflags "$$@")"; \
	    printf 'LDFLAGS += %s\n' "$$(config/pkgconf.sh --ldflags "$$@")"; \
	    printf 'LDLIBS := %s $${LDLIBS}\n' "$$(config/pkgconf.sh --ldlibs "$$@")"; \
	}; \
	gen $$(grep -l ' true$$' ${.ALLSRC} | sed 's|.*/\(.*\)\.h|\1|') >>$@
	${VCAT} $@

.PHONY: ${GEN}/pkgs.mk

# Convert ${GEN}/use/foo.h to foo
PKG = ${@:${GEN}/use/%.h=%}

${HEADERS}::
	${MKDIR} ${@D}
	if config/define-if.sh use/${PKG} config/pkgconf.sh ${PKG} >$@ 2>$@.log; then \
	    test "${IS_V}" || printf '[ CC ] %-${MSG_WIDTH}s  ✔\n' use/${PKG}.c; \
	else \
	    test "${IS_V}" || printf '[ CC ] %-${MSG_WIDTH}s  ✘\n' use/${PKG}.c; \
	fi
