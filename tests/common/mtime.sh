cd "$TEST"

now=$(epoch_time)

"$XTOUCH" -mt "@$((now - 60 * 60 * 24 * 7))" last_week
"$XTOUCH" -mt "@$((now - 60 * 60 * 49))" two_days_ago
"$XTOUCH" -mt "@$((now - 60 * 60 * 25))" yesterday
"$XTOUCH" -mt "@$((now - 60 * 60))" one_hour_ago
"$XTOUCH" -mt "@$((now))" now
"$XTOUCH" -mt "@$((now + 60 * 60 * 24))" tomorrow

bfs_diff . -mindepth 1 \
    \( -mtime -1 -exec printf -- '-mtime -1: %s\n' {} \; -o -true \) \
    \( -mtime  1 -exec printf -- '-mtime  1: %s\n' {} \; -o -true \) \
    \( -mtime +1 -exec printf -- '-mtime +1: %s\n' {} \; -o -true \)
