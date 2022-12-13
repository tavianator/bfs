clean_scratch
mkfifo scratch/{pid,hup}

(
    # Create a zombie process
    echo >/dev/null &
    # Write the PID to scratch/pid
    echo $! >scratch/pid
    # Don't wait on the processes
    exec cat scratch/hup >/dev/null
) &

# Kill cat on exit
trap "echo >scratch/hup" EXIT

# Read the zombie PID
read -r pid <scratch/pid

# On Linux, open(/proc/$pid/net) will succeed but readdir() will fail
skip_unless test -r "/proc/$pid/net"
fail invoke_bfs "/proc/$pid/net" >/dev/null
