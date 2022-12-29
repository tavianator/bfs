test "$UNAME" = "Linux" || skip

clean_scratch

invoke_bfs scratch -quit -capable || skip

"$XTOUCH" scratch/{normal,capable}
bfs_sudo setcap all+ep scratch/capable || skip
ln -s capable scratch/link

bfs_diff scratch -capable
