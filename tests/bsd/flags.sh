invoke_bfs scratch -quit -flags offline || skip

clean_scratch

"$XTOUCH" scratch/{foo,bar}
chflags offline scratch/bar || skip

bfs_diff scratch -flags -offline,nohidden
