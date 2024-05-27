invoke_bfs -regextype egrep -quit || skip

bfs_diff weirdnames -regextype egrep -regex '*.*/{l'
