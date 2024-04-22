# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that generates gen/config.h

include config/prelude.mk
include ${GEN}/config.mk
include config/exports.mk

# All header fragments we generate
HEADERS := \
    ${GEN}/acl-is-trivial-np.h \
    ${GEN}/aligned-alloc.h \
    ${GEN}/confstr.h \
    ${GEN}/extattr-get-file.h \
    ${GEN}/extattr-get-link.h \
    ${GEN}/extattr-list-file.h \
    ${GEN}/extattr-list-link.h \
    ${GEN}/fdclosedir.h \
    ${GEN}/getdents.h \
    ${GEN}/getdents64.h \
    ${GEN}/getdents64-syscall.h \
    ${GEN}/getprogname.h \
    ${GEN}/getprogname-gnu.h \
    ${GEN}/max-align-t.h \
    ${GEN}/pipe2.h \
    ${GEN}/posix-spawn-addfchdir.h \
    ${GEN}/posix-spawn-addfchdir-np.h \
    ${GEN}/st-acmtim.h \
    ${GEN}/st-acmtimespec.h \
    ${GEN}/st-birthtim.h \
    ${GEN}/st-birthtimespec.h \
    ${GEN}/st-flags.h \
    ${GEN}/statx.h \
    ${GEN}/statx-syscall.h \
    ${GEN}/strerror-l.h \
    ${GEN}/strerror-r-gnu.h \
    ${GEN}/strerror-r-posix.h \
    ${GEN}/tm-gmtoff.h \
    ${GEN}/uselocale.h

${GEN}/config.h: ${HEADERS}
	${MSG} "[ GEN] ${TGT}"
	printf '// %s\n' "${TGT}" >$@
	printf '#ifndef BFS_CONFIG_H\n' >>$@
	printf '#define BFS_CONFIG_H\n' >>$@
	cat ${.ALLSRC} >>$@
	printf '#endif // BFS_CONFIG_H\n' >>$@
	cat ${.ALLSRC:%=%.log} >$@.log
	${RM} ${.ALLSRC} ${.ALLSRC:%=%.log}
	${VCAT} $@
.PHONY: ${GEN}/config.h

# The C source file to attempt to compile
CSRC = ${@:${GEN}/%.h=config/%.c}

${HEADERS}::
	config/cc-define.sh ${CSRC} >$@ 2>$@.log
	if ! [ "${IS_V}" ]; then \
	    if grep -q 'true$$' $@; then \
	        printf '[ CC ] %-${MSG_WIDTH}s  ✔\n' ${CSRC}; \
	    else \
	        printf '[ CC ] %-${MSG_WIDTH}s  ✘\n' ${CSRC}; \
	    fi; \
	fi
