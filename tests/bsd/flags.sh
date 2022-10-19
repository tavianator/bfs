skip_unless invoke_bfs scratch -quit -flags offline

rm -rf scratch/*

$TOUCH scratch/{foo,bar}
skip_unless chflags offline scratch/bar

bfs_diff scratch -flags -offline,nohidden
