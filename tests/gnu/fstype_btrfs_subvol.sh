# Test that -fstype works in btrfs subvolumes

command -v btrfs &>/dev/null || skip

cd "$TEST"

# Make a btrfs filesystem image
truncate -s128M img
mkfs.btrfs img >&2

# Mount it
mkdir mnt
bfs_sudo mount img mnt || skip
defer bfs_sudo umount mnt

# Make it owned by us
bfs_sudo chown "$(id -u):$(id -g)" mnt

# Create a subvolume inside it
btrfs subvolume create mnt/subvol >&2

# Make a file in and outside the subvolume
"$XTOUCH" mnt/file mnt/subvol/file

bfs_diff mnt -fstype btrfs -print -o -printf '%p %F\n'
