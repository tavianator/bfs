cd "$TEST"

"$XTOUCH" aab aba

# a*(ab)* can match the "aa" prefix of "aab", but we need to look for a complete match
bfs_diff . -regex '.*/a*\(ab\)*'
