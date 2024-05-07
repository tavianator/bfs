# Only ffs supports whiteouts on FreeBSD
command -v mdconfig &>/dev/null || skip
command -v newfs &>/dev/null || skip

cd "$TEST"

# Create a ramdisk
if command -v truncate &>/dev/null; then
    truncate -s1M img
else
    dd if=/dev/zero of=img bs=1k count=1k
fi
md=$(bfs_sudo mdconfig img) || skip
defer bfs_sudo mdconfig -du "$md"

# Make an ffs filesystem
bfs_sudo newfs -n "/dev/$md" >&2 || skip
mkdir mnt

# Mount it
bfs_sudo mount "/dev/$md" mnt || skip
defer bfs_sudo umount mnt

# Make it owned by us
bfs_sudo chown "$(id -u):$(id -g)" mnt
"$XTOUCH" -p mnt/{lower/{foo,bar,baz},upper/{bar,baz/qux}}

# Mount a union filesystem within it
bfs_sudo mount -t unionfs -o below mnt/{lower,upper}
defer bfs_sudo umount mnt/upper

# Create a whiteout
rm mnt/upper/bar

# FreeBSD find doesn't have -printf, so munge -ls output
munge_ls() {
    sed -En 's|.*([-drwx]{10}).*(mnt/.*)|'"$1"': \1 \2|p'
}

# Do a few tests in one
{
    # Normally, we shouldn't see the whiteouts
    invoke_bfs mnt -ls | munge_ls 1
    # -type w adds whiteouts to the output
    invoke_bfs mnt -type w -ls | munge_ls 2
    # So this is not the same as test 1
    invoke_bfs mnt \( -type w -or -not -type w \) -ls | munge_ls 3
    # Unmount the unionfs
    pop_defer
    # Now repeat the same tests
    invoke_bfs mnt -ls | munge_ls 4
    invoke_bfs mnt -type w -ls | munge_ls 5
    invoke_bfs mnt \( -type w -or -not -type w \) -ls | munge_ls 6
} >"$OUT"
sort_output
diff_output
