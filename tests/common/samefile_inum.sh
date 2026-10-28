# Regression test: ! -samefile foo shouldn't imply that the inode number can't match

test "$UNAME" = "Linux" || skip

cd "$TEST"
mkdir mnt1 mnt2

truncate -s1M img1
mkfs.ext2 -q img1 || skip
cp img1 img2

bfs_sudo mount -o loop img1 mnt1 || skip
defer bfs_sudo umount mnt1

bfs_sudo mount -o loop img2 mnt2 || skip
defer bfs_sudo umount mnt2

test "$(inum mnt1)" = "$(inum mnt2)" || skip

bfs_diff mnt1 mnt2 -maxdepth 0 -inum "$(inum mnt1)" ! -samefile mnt1
