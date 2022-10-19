# fnmatch() is broken on macOS
skip_if test "$UNAME" = "Darwin"

# An unclosed [ should be matched literally
bfs_diff weirdnames -name '['
