clean_scratch
"$XTOUCH" -p scratch/foo/bar/baz
ln -s foo/bar/baz scratch/link
ln -s foo/bar/nowhere scratch/broken
ln -s foo/bar/nowhere/nothing scratch/nested
ln -s foo/bar/baz//qux scratch/notdir
ln -s scratch/foo/bar scratch/relative
mkdir scratch/__bfs__
ln -s /__bfs__/nowhere scratch/absolute

export LS_COLORS="or=01;31:"
invoke_bfs scratch/{,link,broken,nested,notdir,relative,absolute} -color -type l -ls \
    | sed 's/.* -> //' \
    | sort >"$OUT"
diff_output
