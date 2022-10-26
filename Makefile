all: animation-dark.svg animation-light.svg
.PHONY: all

animation-%.svg: animation.cast template-%.svg
	termtosvg render $< $@ -t template-$*.svg
