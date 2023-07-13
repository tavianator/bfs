name="0123456789ABCDEF"
name="${name}${name}${name}${name}"
name="${name}${name}${name}${name}"
name="${name:0:255}"
export LS_COLORS="*${name}=01:"

bfs_diff deep -color -type f -printf '%f\n'
