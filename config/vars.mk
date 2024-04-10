# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile fragment loads and exports variables for config steps

GEN := ${BUILDDIR}/gen
GEN := ${GEN:./%=%}

include ${GEN}/vars.mk

_CC := ${CC}
_CPPFLAGS := ${CPPFLAGS}
_CFLAGS := ${CFLAGS}
_LDFLAGS := ${LDFLAGS}
_LDLIBS := ${LDLIBS}

export CC=${_CC}
export CPPFLAGS=${_CPPFLAGS}
export CFLAGS=${_CFLAGS}
export LDFLAGS=${_LDFLAGS}
export LDLIBS=${_LDLIBS}
