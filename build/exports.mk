# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile fragment that exports variables used by configuration scripts

export XPREFIX=${PREFIX}
export XMANDIR=${MANDIR}

export XCC=${CC}
export XINSTALL=${INSTALL}
export XMKDIR=${MKDIR}
export XPKG_CONFIG=${PKG_CONFIG}
export XRM=${RM}

export XCPPFLAGS=${_CPPFLAGS}
export XCFLAGS=${_CFLAGS}
export XLDFLAGS=${_LDFLAGS}
export XLDLIBS=${_LDLIBS}

export CONF_CPPFLAGS=${CPPFLAGS} ${EXTRA_CPPFLAGS}
export CONF_CFLAGS=${CFLAGS} ${EXTRA_CFLAGS}
export CONF_LDFLAGS=${LDFLAGS} ${EXTRA_LDFLAGS}
export CONF_LDLIBS=${LDLIBS} ${EXTRA_LDLIBS}

export XNOLIBS=${NOLIBS}
