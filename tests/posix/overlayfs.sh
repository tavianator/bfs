test "$UNAME" = "Linux" || skip

clean_scratch
"$XTOUCH" -p scratch/{lower/{foo,bar,baz},upper/{bar,baz/qux}}

mkdir -p scratch/{work,merged}
bfs_sudo mount -t overlay overlay -olowerdir=scratch/lower,upperdir=scratch/upper,workdir=scratch/work scratch/merged || skip
defer bfs_sudo rm -rf scratch/work
defer bfs_sudo umount scratch/merged

bfs_diff scratch/merged
