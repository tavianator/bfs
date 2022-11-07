skip_unless test "$SUDO"
skip_unless test "$UNAME" = "Linux"

clean_scratch

skip_unless invoke_bfs scratch -quit -capable

"$XTOUCH" scratch/{normal,capable}
sudo setcap all+ep scratch/capable
ln -s capable scratch/link

bfs_diff scratch -capable
