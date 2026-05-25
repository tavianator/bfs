# Test for https://www.austingroupbugs.net/view.php?id=1392
umask 002
bfs_diff perms -perm -+w
