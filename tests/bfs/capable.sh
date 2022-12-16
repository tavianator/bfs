test "$SUDO" || skip
test "$UNAME" = "Linux" || skip

clean_scratch

invoke_bfs scratch -quit -capable || skip

"$XTOUCH" scratch/{normal,capable}
sudo setcap all+ep scratch/capable
ln -s capable scratch/link

bfs_diff scratch -capable
