# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that generates gen/deps.mk

include config/prelude.mk
include ${GEN}/vars.mk
include ${GEN}/flags.mk
include config/exports.mk

${GEN}/deps.mk::
	${MSG} "[ GEN] $@"
	printf '# %s\n' "$@" >$@
	if config/cc.sh -MD -MP -MF /dev/null config/empty.c; then \
	    printf 'DEPFLAGS = -MD -MP -MF $${@:.o=.d}\n'; \
	fi >>$@ 2>$@.log
	${VCAT} $@
	@printf -- '-include %s\n' ${OBJS:.o=.d} >>$@