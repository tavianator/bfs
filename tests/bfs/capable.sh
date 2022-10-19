skip_unless test "$SUDO"
skip_unless test "$UNAME" = "Linux"

rm -rf scratch/*

skip_unless invoke_bfs scratch -quit -capable

$TOUCH scratch/{normal,capable}
sudo setcap all+ep scratch/capable
ln -s capable scratch/link

bfs_diff scratch -capable
