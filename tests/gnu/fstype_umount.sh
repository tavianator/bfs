test "$UNAME" = "Linux" || skip

clean_scratch

mkdir scratch/tmp
bfs_sudo mount -t tmpfs tmpfs scratch/tmp || skip
trap "bfs_sudo umount -R scratch/tmp" EXIT

mkdir scratch/tmp/ram
bfs_sudo mount -t ramfs ramfs scratch/tmp/ram || skip

bfs_diff scratch/tmp -path scratch/tmp -exec "${SUDO[@]}" umount scratch/tmp/ram \; , -fstype ramfs -print
