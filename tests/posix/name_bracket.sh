# fnmatch() is broken on macOS
test "$UNAME" = "Darwin" && skip

# An unclosed [ should be matched literally
bfs_diff weirdnames -name '['
