rm -rf scratch/*

# Incomplete UTF-8 sequences
skip_unless touch scratch/$'\xC3'
skip_unless touch scratch/$'\xE2\x84'
skip_unless touch scratch/$'\xF0\x9F\x92'

bfs_diff scratch -regex 'scratch/..'
