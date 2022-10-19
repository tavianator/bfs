# POSIX says:
#     Only a <plus-sign> that immediately follows an argument containing only the two characters "{}"
#     shall punctuate the end of the primary expression. Other uses of the <plus-sign> shall not be
#     treated as special.
bfs_diff basic -exec echo foo {} bar + baz \;
