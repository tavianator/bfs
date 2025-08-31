# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that implements `./configure`

include build/prelude.mk
include build/exports.mk

# All configuration steps
config: gen/config.mk gen/config.h
.PHONY: config

# All the generated makefile fragments
MKS := \
    gen/vars.mk \
    gen/early.mk \
    gen/auto.mk \
    gen/pkgs.mk \
    gen/late.mk

# The main configuration file, which includes the others
gen/config.mk: ${MKS}
	${MSG} "[ GEN] $@"
	@printf '# %s\n' "$@" >$@
	@printf 'include %s\n' $^ >>$@
	${VCAT} $@
.PHONY: gen/config.mk

# Saves the configurable variables
gen/vars.mk::
	@${MKDIR} ${@D}
	${MSG} "[ GEN] $@"
	@printf '# %s\n' "$@" >$@
	@printf 'PREFIX := %s\n' "$$XPREFIX" >>$@
	@printf 'MANDIR := %s\n' "$$XMANDIR" >>$@
	@printf 'OS := %s\n' "$${OS:-$$(uname)}" >>$@
	@printf 'CC := %s\n' "$$XCC" >>$@
	@printf 'INSTALL := %s\n' "$$XINSTALL" >>$@
	@printf 'MKDIR := %s\n' "$$XMKDIR" >>$@
	@printf 'PKG_CONFIG := %s\n' "$$XPKG_CONFIG" >>$@
	@printf 'RM := %s\n' "$$XRM" >>$@
	@test -z "$$VERSION" || printf 'export VERSION=%s\n' "$$VERSION" >>$@
	${VCAT} $@

# Sets the build flags.  This depends on vars.mk and uses a recursive make so
# that the default flags can depend on variables like ${OS}.
gen/early.mk gen/late.mk: gen/vars.mk
	@+XMAKEFLAGS="$$MAKEFLAGS" ${MAKE} -sf build/flags.mk $@

# Auto-detected build flags
gen/auto.mk: gen/early.mk gen/late.mk
	@+XMAKEFLAGS="$$MAKEFLAGS" ${MAKE} -sf build/auto.mk $@

# Auto-detect dependencies and their build flags
gen/pkgs.mk: gen/auto.mk
	@+XMAKEFLAGS="$$MAKEFLAGS" ${MAKE} -sf build/pkgs.mk $@

# Compile-time feature detection
gen/config.h: gen/pkgs.mk
	@+XMAKEFLAGS="$$MAKEFLAGS" ${MAKE} -sf build/header.mk $@
