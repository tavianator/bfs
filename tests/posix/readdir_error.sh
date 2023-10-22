test "$UNAME" = "Linux" || skip

cd "$TEST"
mkfifo hang pid wait running

(
    # Create a zombie process
    cat hang >/dev/null &
    # Write the PID to pid
    echo $! >pid
    # Don't wait on the zombie process
    exec cat wait hang >running
) &

# Kill the parent cat on exit
defer kill -9 %1

# Read the child PID
read -r pid <pid

# Make sure the parent cat is running before we kill the child, because bash
# will wait() on its children
echo >wait &
read -r _ <running

# Turn the child into a zombie
kill -9 "$pid"

# Wait until it's really a zombie
state=R
while [ "$state" != "Z" ]; do
    read -r _ _ state _ <"/proc/$pid/stat"
done

# On Linux, open(/proc/$pid/net) will succeed but readdir() will fail
test -r "/proc/$pid/net" || skip
! invoke_bfs "/proc/$pid/net" >/dev/null
