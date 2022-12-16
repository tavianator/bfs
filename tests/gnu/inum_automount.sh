# bfs shouldn't trigger automounts unless it descends into them

test "$SUDO" || skip
command -v systemd-mount &>/dev/null || skip

clean_scratch
mkdir scratch/{foo,automnt}

sudo systemd-mount -A -o bind basic scratch/automnt || skip
trap "sudo systemd-umount scratch/automnt" EXIT

before=$(inum scratch/automnt)
bfs_diff scratch -inum "$before" -prune
after=$(inum scratch/automnt)
((before == after))
