test "$UNAME" = "Linux" || skip
invoke_bfs . -quit -capable || skip

cd "$TEST"

"$XTOUCH" normal capable
bfs_sudo setcap all+ep capable || skip
ln -s capable link

LS_COLORS="ca=30;41:" bfs_diff . -color
