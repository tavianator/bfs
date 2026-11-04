cd "$TEST"

mkdir -p foo bar/baz

cat >foo/baz <<EOF
#!/bin/sh
printf '%s\n' "\$@"
EOF
chmod +x foo/baz

PATH="bar:foo:$PATH" bfs_diff . -exec baz {} \;
