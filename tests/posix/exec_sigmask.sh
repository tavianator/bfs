# Regression test: restore the signal mask after fork()

cd "$TEST"
mkfifo p1 p2

{
    # Get the PID of `sh`
    read -r pid <p1
    # Send SIGTERM -- this will hang forever if signals are blocked
    kill $pid
} &

# Write the `sh` PID to p1, then hang reading p2 until we're killed
! invoke_bfs p1 -exec bash -c 'echo $$ >p1 && read -r _ <p2' bash {} + || fail

_wait
