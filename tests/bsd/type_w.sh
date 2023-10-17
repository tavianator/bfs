# Only ffs supports whiteouts on FreeBSD
command -v mdconfig &>/dev/null || skip
command -v newfs &>/dev/null || skip

cleanup=()
do_cleanup() {
    # Run cleanup hooks in reverse order
    while ((${#cleanup[@]} > 0)); do
        cmd="${cleanup[-1]}"
        unset 'cleanup[-1]'
        eval "bfs_sudo $cmd"
    done
}
trap do_cleanup EXIT

clean_scratch

# Create a ramdisk
truncate -s1M scratch/img
md=$(bfs_sudo mdconfig scratch/img) || skip
cleanup+=("mdconfig -du $md")

# Make an ffs filesystem
bfs_sudo newfs -n "/dev/$md" >&2 || skip
mkdir scratch/mnt

# Mount it
bfs_sudo mount "/dev/$md" scratch/mnt || skip
cleanup+=("umount scratch/mnt")

# Make it owned by us
bfs_sudo chown "$(id -u):$(id -g)" scratch/mnt
"$XTOUCH" -p scratch/mnt/{lower/{foo,bar,baz},upper/{bar,baz/qux}}

# Mount a union filesystem within it
bfs_sudo mount -t unionfs -o below scratch/mnt/{lower,upper}
cleanup+=("umount scratch/mnt/upper")

# Create a whiteout
rm scratch/mnt/upper/bar

# FreeBSD find doesn't have -printf, so munge -ls output
munge_ls() {
    sed -En 's|.*([-drwx]{10}).*(scratch/.*)|'"$1"': \1 \2|p'
}

# Do a few tests in one
{
    # Normally, we shouldn't see the whiteouts
    invoke_bfs scratch/mnt -ls | munge_ls 1
    # -type w adds whiteouts to the output
    invoke_bfs scratch/mnt -type w -ls | munge_ls 2
    # So this is not the same as test 1
    invoke_bfs scratch/mnt \( -type w -or -not -type w \) -ls | munge_ls 3
    # Unmount the unionfs
    bfs_sudo umount scratch/mnt/upper
    unset 'cleanup[-1]'
    # Now repeat the same tests
    invoke_bfs scratch/mnt -ls | munge_ls 4
    invoke_bfs scratch/mnt -type w -ls | munge_ls 5
    invoke_bfs scratch/mnt \( -type w -or -not -type w \) -ls | munge_ls 6
} >"$OUT"
sort_output
diff_output
