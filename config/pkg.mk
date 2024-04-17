# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that generates gen/lib*.mk

include config/prelude.mk
include ${GEN}/vars.mk
include ${GEN}/flags.mk
include config/exports.mk

# Like ${TGT} but for ${TARGET}, not $@
SHORT = ${TARGET:${BUILDDIR}/%=%}

default::
	@printf '# %s\n' "${TARGET}" >${TARGET}
	config/pkg.sh ${TARGET:${GEN}/%.mk=%} >>${TARGET} 2>${TARGET}.log
	@if [ "${IS_V}" ]; then \
	    cat ${TARGET}; \
	elif grep -q PKGS ${TARGET}; then \
	    printf '[ GEN] %-18s [y]\n' ${SHORT}; \
	else \
	    printf '[ GEN] %-18s [n]\n' ${SHORT}; \
	fi
