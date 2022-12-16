clean_scratch

invoke_bfs scratch -quit -acl || skip

"$XTOUCH" scratch/{normal,acl}
set_acl scratch/acl || skip
ln -s acl scratch/link

bfs_diff -L scratch -acl
