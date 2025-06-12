cd "$TEST"
mkdir foo
cat >foo/bar.sh <<EOF
#!/bin/sh
printf '%s\n' "\$@"
EOF
chmod +x foo/bar.sh

bfs_diff . -name bar.sh -execdir {} {} \;
