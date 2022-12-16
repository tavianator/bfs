test "$UNAME" = "Linux" || skip

clean_scratch
mkfifo scratch/{fever,pid,wait,running}

(
    # Create a zombie process
    cat scratch/fever >/dev/null &
    # Write the PID to scratch/pid
    echo $! >scratch/pid
    # Don't wait on the zombie process
    exec cat scratch/wait scratch/fever >scratch/running
) &

# Kill the parent cat on exit
trap "kill -9 %1" EXIT

# Read the child PID
read -r pid <scratch/pid

# Make sure the parent cat is running before we kill the child, because bash
# will wait() on its children
echo >scratch/wait &
read -r _ <scratch/running

# Turn the child into a zombie
kill -9 "$pid"

# Wait until it's really a zombie
state=R
while [ "$state" != "Z" ]; do
    read -r _ _ state _ <"/proc/$pid/stat" || exit 1
done

# On Linux, open(/proc/$pid/net) will succeed but readdir() will fail
test -r "/proc/$pid/net" || skip
fail invoke_bfs "/proc/$pid/net" >/dev/null
