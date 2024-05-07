cd "$TEST"

invoke_bfs . -quit -acl || skip

"$XTOUCH" normal acl
set_acl acl || skip
ln -s acl link

bfs_diff -L . -acl
