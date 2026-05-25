# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that generates gen/auto.mk

include build/prelude.mk
include gen/vars.mk
include gen/early.mk
include gen/late.mk
include build/exports.mk

# Auto-detected flags
AUTO_FLAGS := \
    gen/flags/std.mk \
    gen/flags/bind-now.mk \
    gen/flags/deps.mk \
    gen/flags/pthread.mk \
    gen/flags/Wformat.mk \
    gen/flags/Wimplicit-fallthrough.mk \
    gen/flags/Wimplicit.mk \
    gen/flags/Wmissing-decls.mk \
    gen/flags/Wmissing-var-decls.mk \
    gen/flags/Wshadow.mk \
    gen/flags/Wsign-compare.mk \
    gen/flags/Wstrict-prototypes.mk \
    gen/flags/Wundef-prefix.mk

gen/auto.mk: ${AUTO_FLAGS}
	${MSG} "[ GEN] $@"
	@printf '# %s\n' "$@" >$@
	@cat $^ >>$@
	@cat ${^:%=%.log} >gen/flags.log
	${VCAT} $@
.PHONY: gen/auto.mk

# Check that the C compiler works at all
cc::
	@build/cc.sh -q build/empty.c -o gen/.cc.out; \
	    ret=$$?; \
	    build/msg-if.sh "[ CC ] build/empty.c" test $$ret -eq 0; \
	    exit $$ret

# The short name of the config test
SLUG = ${@:gen/%.mk=%}
# The source file to build
CSRC = build/${SLUG}.c
# The hidden output file name
OUT = ${SLUG:flags/%=gen/flags/.%.out}

${AUTO_FLAGS}: cc
	@${MKDIR} ${@D}
	@build/flags-if.sh ${CSRC} -o ${OUT} >$@ 2>$@.log; \
	    build/msg-if.sh "[ CC ] ${SLUG}.c" test $$? -eq 0
.PHONY: ${AUTO_FLAGS}
