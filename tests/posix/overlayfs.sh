test "$UNAME" = "Linux" || skip

cd "$TEST"
"$XTOUCH" -p lower/{foo,bar,baz} upper/{bar,baz/qux}

mkdir -p work merged
bfs_sudo mount -t overlay overlay -olowerdir=lower,upperdir=upper,workdir=work merged || skip
defer bfs_sudo rm -rf work
defer bfs_sudo umount merged

bfs_diff merged
