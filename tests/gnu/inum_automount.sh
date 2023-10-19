# bfs shouldn't trigger automounts unless it descends into them

command -v systemd-mount &>/dev/null || skip

clean_scratch
mkdir scratch/{foo,automnt}

bfs_sudo systemd-mount -A -o bind basic scratch/automnt || skip
defer bfs_sudo systemd-umount scratch/automnt

before=$(inum scratch/automnt)
bfs_diff scratch -inum "$before" -prune
after=$(inum scratch/automnt)
((before == after))
