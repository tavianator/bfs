[[ "$UNAME" == *BSD* ]] || skip
clean_scratch
"$XTOUCH" -p scratch/{lower/{foo,bar,baz},upper/{bar,baz/qux}}
bfs_sudo mount -t unionfs -o below scratch/{lower,upper} || skip
trap "bfs_sudo umount scratch/upper" EXIT
bfs_diff scratch
