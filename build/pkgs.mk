# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that generates gen/pkgs.mk

include build/prelude.mk
include gen/vars.mk
include gen/flags.mk
include build/exports.mk

HEADERS := ${ALL_PKGS:%=gen/with/%.h}

gen/pkgs.mk: ${HEADERS}
	${MSG} "[ GEN] $@"
	@printf '# %s\n' "$@" >$@
	@gen() { \
	    printf 'PKGS := %s\n' "$$*"; \
	    printf '_CFLAGS += %s\n' "$$(build/pkgconf.sh --cflags "$$@")"; \
	    printf '_LDFLAGS += %s\n' "$$(build/pkgconf.sh --ldflags "$$@")"; \
	    printf '_LDLIBS := %s $${_LDLIBS}\n' "$$(build/pkgconf.sh --ldlibs "$$@")"; \
	}; \
	gen $$(grep -l ' true$$' $^ | sed 's|.*/\(.*\)\.h|\1|') >>$@
	${VCAT} $@

.PHONY: gen/pkgs.mk

# Convert gen/with/foo.h to foo
PKG = ${@:gen/with/%.h=%}

${HEADERS}::
	@${MKDIR} ${@D}
	@build/define-if.sh with/${PKG} build/pkgconf.sh ${PKG} >$@ 2>$@.log; \
	    build/msg-if.sh "[ CC ] with/${PKG}.c" test $$? -eq 0;
