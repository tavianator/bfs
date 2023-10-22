cd "$TEST"

# Incomplete UTF-8 sequences
touch $'\xC3' || skip
touch $'\xE2\x84' || skip
touch $'\xF0\x9F\x92' || skip

bfs_diff . -regex '\./..'
