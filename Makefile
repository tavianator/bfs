all: animation.svg

%.svg: %.cast
	termtosvg render $< $@ -t $*_template.svg
