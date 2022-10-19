#     The -ok primary shall be equivalent to -exec, except that the use of a
#     <plus-sign> to punctuate the end of the primary expression need not be
#     supported, ...
#
# bfs chooses not to support it, for compatibility with most other find
# implementations.

yes | bfs_diff basic -ok echo {} + \;
