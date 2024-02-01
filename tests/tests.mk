# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# GNU makefile that exposes make's job control to tests.sh

tests/%:
	bash -c 'printf . >&$(READY) && read -r -N1 -u$(DONE)'
