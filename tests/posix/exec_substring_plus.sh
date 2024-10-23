# https://pubs.opengroup.org/onlinepubs/9799919799/utilities/find.html
#
#     Only a <plus-sign> that immediately follows an argument containing only
#     the two characters "{}" shall punctuate the end of the primary expression.
#     Other uses of the <plus-sign> shall not be treated as special.
#     ...
#     If a utility_name or argument string contains the two characters "{}", but
#     not just the two characters "{}", it is implementation-defined whether
#     find replaces those two characters or uses the string without change.

invoke_bfs basic -exec printf '%s %s %s %s\n' {} {}+ +{} + \; | sed 's/ .*//' >"$OUT"
sort_output
diff_output

