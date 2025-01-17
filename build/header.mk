# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that generates gen/config.h

include build/prelude.mk
include gen/vars.mk
include gen/flags.mk
include gen/pkgs.mk
include build/exports.mk

# All header fragments we generate
HEADERS := \
    gen/has/--st-birthtim.h \
    gen/has/_Fork.h \
    gen/has/acl-get-entry.h \
    gen/has/acl-get-file.h \
    gen/has/acl-get-tag-type.h \
    gen/has/acl-is-trivial-np.h \
    gen/has/acl-trivial.h \
    gen/has/builtin-riscv-pause.h \
    gen/has/compound-literal-storage.h \
    gen/has/confstr.h \
    gen/has/extattr-get-file.h \
    gen/has/extattr-get-link.h \
    gen/has/extattr-list-file.h \
    gen/has/extattr-list-link.h \
    gen/has/fdclosedir.h \
    gen/has/getdents.h \
    gen/has/getdents64-syscall.h \
    gen/has/getdents64.h \
    gen/has/getmntent-1.h \
    gen/has/getmntent-2.h \
    gen/has/getmntinfo.h \
    gen/has/getprogname-gnu.h \
    gen/has/getprogname.h \
    gen/has/io-uring-max-workers.h \
    gen/has/pipe2.h \
    gen/has/pragma-nounroll.h \
    gen/has/posix-getdents.h \
    gen/has/posix-spawn-addfchdir-np.h \
    gen/has/posix-spawn-addfchdir.h \
    gen/has/pthread-set-name-np.h \
    gen/has/pthread-setname-np.h \
    gen/has/st-acmtim.h \
    gen/has/st-acmtimespec.h \
    gen/has/st-birthtim.h \
    gen/has/st-birthtimespec.h \
    gen/has/st-flags.h \
    gen/has/statx-syscall.h \
    gen/has/statx.h \
    gen/has/strerror-l.h \
    gen/has/strerror-r-gnu.h \
    gen/has/strerror-r-posix.h \
    gen/has/string-to-flags.h \
    gen/has/strtofflags.h \
    gen/has/tcgetwinsize.h \
    gen/has/timegm.h \
    gen/has/timer-create.h \
    gen/has/tm-gmtoff.h \
    gen/has/uselocale.h

# Previously generated by pkgs.mk
PKG_HEADERS := ${ALL_PKGS:%=gen/with/%.h}

gen/config.h: ${PKG_HEADERS} ${HEADERS}
	${MSG} "[ GEN] $@"
	@printf '// %s\n' "$@" >$@
	@printf '#ifndef BFS_CONFIG_H\n' >>$@
	@printf '#define BFS_CONFIG_H\n' >>$@
	@cat ${.ALLSRC} >>$@
	@printf '#endif // BFS_CONFIG_H\n' >>$@
	@cat gen/flags.log ${.ALLSRC:%=%.log} >gen/config.log
	${VCAT} $@
	@printf '%s' "$$CONFFLAGS" | build/embed.sh >gen/confflags.i
	@printf '%s' "$$XCC" | build/embed.sh >gen/cc.i
	@printf '%s' "$$XCPPFLAGS" | build/embed.sh >gen/cppflags.i
	@printf '%s' "$$XCFLAGS" | build/embed.sh >gen/cflags.i
	@printf '%s' "$$XLDFLAGS" | build/embed.sh >gen/ldflags.i
	@printf '%s' "$$XLDLIBS" | build/embed.sh >gen/ldlibs.i
.PHONY: gen/config.h

# The short name of the config test
SLUG = ${@:gen/%.h=%}
# The hidden output file name
OUT = ${SLUG:has/%=gen/has/.%.out}

${HEADERS}::
	@${MKDIR} ${@D}
	@build/define-if.sh ${SLUG} build/cc.sh build/${SLUG}.c -o ${OUT} >$@ 2>$@.log; \
	    build/msg-if.sh "[ CC ] ${SLUG}.c" test $$? -eq 0
