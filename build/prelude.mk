# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Common makefile utilities.  Compatible with both GNU make and most BSD makes.

# BSD make will chdir into ${.OBJDIR} by default, unless we tell it not to
.OBJDIR: .

# We don't use any suffix rules
.SUFFIXES:

# GNU make has $^ for the full list of targets, while BSD make has $> and the
# long-form ${.ALLSRC}.  We could write $^ $> to get them both, but that would
# break if one of them implemented support for the other.  So instead, bring
# BSD's ${.ALLSRC} to GNU.
.ALLSRC ?= $^

# Installation paths
DESTDIR ?=
PREFIX ?= /usr
MANDIR ?= ${PREFIX}/share/man

# Configurable executables
CC ?= cc
INSTALL ?= install
MKDIR ?= mkdir -p
PKG_CONFIG ?= pkg-config
RM ?= rm -f

# GNU and BSD make have incompatible syntax for conditionals, but we can do a
# lot with just nested variable expansion.  We use "y" as the canonical
# truthy value, and "" (the empty string) as the canonical falsey value.
#
# To normalize a boolean, use ${TRUTHY,${VAR}}, which expands like this:
#
#     VAR=y      ${TRUTHY,${VAR}} => ${TRUTHY,y}     => y
#     VAR=1      ${TRUTHY,${VAR}} => ${TRUTHY,1}     => y
#     VAR=n      ${TRUTHY,${VAR}} => ${TRUTHY,n}     =>   [empty]
#     VAR=other  ${TRUTHY,${VAR}} => ${TRUTHY,other} =>   [empty]
#     VAR=       ${TRUTHY,${VAR}} => ${TRUTHY,}      =>   [empty]
#
# Inspired by https://github.com/wahern/autoguess
TRUTHY,y := y
TRUTHY,1 := y

# Boolean operators are also implemented with nested expansion
NOT, := y

# Normalize ${V} to either "y" or ""
export XV=${TRUTHY,${V}}

# Suppress output unless V=1
Q, := @
Q  := ${Q,${XV}}

# Show full commands with `make V=1`, otherwise short summaries
MSG = @build/msg.sh

# cat a file if V=1
VCAT,y := @cat
VCAT,  := @:
VCAT   := ${VCAT,${XV}}

# All external dependencies
ALL_PKGS := \
    libacl \
    libcap \
    libselinux \
    liburing \
    oniguruma
