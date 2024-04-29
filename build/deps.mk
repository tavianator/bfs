# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that generates gen/deps.mk

include build/prelude.mk
include gen/vars.mk
include gen/flags.mk
include build/exports.mk

gen/deps.mk::
	${MSG} "[ GEN] $@"
	printf '# %s\n' "$@" >$@
	if build/cc.sh -MD -MP -MF /dev/null build/empty.c; then \
	    printf 'DEPFLAGS := -MD -MP\n'; \
	fi >>$@ 2>$@.log
	${VCAT} $@
	printf -- '-include %s\n' ${OBJS:.o=.d} >>$@
