# bfs shouldn't trigger automounts unless it descends into them

skip_unless test "$SUDO"
skip_unless command -v systemd-mount &>/dev/null

rm -rf scratch/*
mkdir scratch/{foo,mnt}
skip_unless sudo systemd-mount -A -o bind basic scratch/mnt

before=$(inum scratch/mnt)
bfs_diff scratch -inum "$before" -prune
ret=$?
after=$(inum scratch/mnt)

sudo systemd-umount scratch/mnt

((ret == 0 && before == after))
