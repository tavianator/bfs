cd "$TEST"

now=$(epoch_time)

"$XTOUCH" -mt "@$((now - 60 * 60))" one_hour_ago
"$XTOUCH" -mt "@$((now - 121))" two_minutes_ago
"$XTOUCH" -mt "@$((now - 61))" one_minute_ago
"$XTOUCH" -mt "@$((now - 30))" thirty_seconds_ago
"$XTOUCH" -mt "@$((now + 60))" in_one_minute
"$XTOUCH" -mt "@$((now + 60 * 60))" in_one_hour

bfs_diff -mindepth 1 \
    \( -mmin -1 -exec printf -- '-mmin -1: %s\n' {} \; -o -true \) \
    \( -mmin  1 -exec printf -- '-mmin  1: %s\n' {} \; -o -true \) \
    \( -mmin +1 -exec printf -- '-mmin +1: %s\n' {} \; -o -true \)
