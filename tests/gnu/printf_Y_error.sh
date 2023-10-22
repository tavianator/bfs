cd "$TEST"
mkdir foo
ln -s foo/bar bar

chmod -x foo
defer chmod +x foo

! bfs_diff . -printf '(%p) (%l) %y %Y\n'
