# *.gz=01;31:*.GZ=01;32         -- case sensitive
# *.tAr=01;33:*.TaR=01;33       -- case-insensitive
# *.TAR.gz=01;34:*.tar.GZ=01;35 -- case-sensitive
# *.txt=35:*TXT=36              -- case-insensitive
export LS_COLORS="*.gz=01;31:*.GZ=01;32:*.tAr=01;33:*.TaR=01;33:*.TAR.gz=01;34:*.tar.GZ=01;35:*.txt=35:*TXT=36"
bfs_diff rainbow -color
