# fnmatch() is broken on some platforms
case "$UNAME" in
    Darwin|NetBSD)
        skip
        ;;
esac

# An unclosed [ should be matched literally
bfs_diff weirdnames -name '['
