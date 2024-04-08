# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that exposes make's job control to tests.sh

# BSD make will chdir into ${.OBJDIR} by default, unless we tell it not to
.OBJDIR: .

# Turn off implicit rules
.SUFFIXES:

.DEFAULT::
	bash -c 'printf . >&$(READY) && read -r -N1 -u$(DONE)'
