test "$UNAME" = "Linux" || skip

cd "$TEST"

mkdir tmp
bfs_sudo mount -t tmpfs tmpfs tmp || skip
defer bfs_sudo umount -R tmp

mkdir tmp/ram
bfs_sudo mount -t ramfs ramfs tmp/ram || skip

bfs_diff tmp -path tmp -exec "${SUDO[@]}" umount tmp/ram \; , -fstype ramfs -print
