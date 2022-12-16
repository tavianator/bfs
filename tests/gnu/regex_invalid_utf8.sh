clean_scratch

# Incomplete UTF-8 sequences
touch scratch/$'\xC3' || skip
touch scratch/$'\xE2\x84' || skip
touch scratch/$'\xF0\x9F\x92' || skip

bfs_diff scratch -regex 'scratch/..'
