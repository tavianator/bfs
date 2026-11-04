# Make sure bfs doesn't die from SIGHUP under nohup

cd "$TEST"
mkfifo ready

bfs_verbose ready -exec sh -c 'echo $PPID >ready; sleep 1' \;
nohup "$PTYX" "${BFS[@]}" ready -exec sh -c 'echo $PPID >ready; sleep 1' \; &>/dev/null &

# Wait until bfs is running the -exec, then hang it up
read -r pid <ready
kill -HUP "$pid"

_wait $!
