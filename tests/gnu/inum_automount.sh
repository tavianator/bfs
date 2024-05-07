# bfs shouldn't trigger automounts unless it descends into them

command -v systemd-mount &>/dev/null || skip

cd "$TEST"
mkdir foo automnt

bfs_sudo systemd-mount -A -o bind "$TMP/basic" automnt || skip
defer bfs_sudo systemd-umount automnt

before=$(inum automnt)
bfs_diff . -inum "$before" -prune
after=$(inum automnt)
((before == after))
