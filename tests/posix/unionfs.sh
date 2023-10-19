[[ "$UNAME" == *BSD* ]] || skip
clean_scratch
"$XTOUCH" -p scratch/{lower/{foo,bar,baz},upper/{bar,baz/qux}}
bfs_sudo mount -t unionfs -o below scratch/{lower,upper} || skip
defer bfs_sudo umount scratch/upper
bfs_diff scratch
