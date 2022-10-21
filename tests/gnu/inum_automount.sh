# bfs shouldn't trigger automounts unless it descends into them

skip_unless test "$SUDO"
skip_unless command -v systemd-mount &>/dev/null

clean_scratch
mkdir scratch/{foo,automnt}
skip_unless sudo systemd-mount -A -o bind basic scratch/automnt

before=$(inum scratch/automnt)
bfs_diff scratch -inum "$before" -prune
ret=$?
after=$(inum scratch/automnt)

sudo systemd-umount scratch/automnt

((ret == 0 && before == after))
