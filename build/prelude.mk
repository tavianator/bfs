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
#     VAR=       ${TRUTHY,${VAR}} => ${TRUTHY,}      =>   [emtpy]
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

# List all object files here, as they're needed by both `./configure` and `make`

# All object files except the entry point
LIBBFS := \
    obj/src/alloc.o \
    obj/src/bar.o \
    obj/src/bfstd.o \
    obj/src/bftw.o \
    obj/src/color.o \
    obj/src/ctx.o \
    obj/src/diag.o \
    obj/src/dir.o \
    obj/src/dstring.o \
    obj/src/eval.o \
    obj/src/exec.o \
    obj/src/expr.o \
    obj/src/fsade.o \
    obj/src/ioq.o \
    obj/src/mtab.o \
    obj/src/opt.o \
    obj/src/parse.o \
    obj/src/printf.o \
    obj/src/pwcache.o \
    obj/src/sighook.o \
    obj/src/stat.o \
    obj/src/thread.o \
    obj/src/trie.o \
    obj/src/typo.o \
    obj/src/xregex.o \
    obj/src/xspawn.o \
    obj/src/xtime.o \
    obj/gen/version.o

# Unit test objects
UNIT_OBJS := \
    obj/tests/alloc.o \
    obj/tests/bfstd.o \
    obj/tests/bit.o \
    obj/tests/ioq.o \
    obj/tests/list.o \
    obj/tests/main.o \
    obj/tests/sighook.o \
    obj/tests/trie.o \
    obj/tests/xspawn.o \
    obj/tests/xtime.o

# All object files
OBJS := \
    obj/src/main.o \
    obj/tests/mksock.o \
    obj/tests/xspawnee.o \
    obj/tests/xtouch.o \
    ${LIBBFS} \
    ${UNIT_OBJS}
