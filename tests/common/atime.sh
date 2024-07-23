cd "$TEST"

now=$(epoch_time)

"$XTOUCH" -at "@$((now - 60 * 60 * 24 * 7))" last_week
"$XTOUCH" -at "@$((now - 60 * 60 * 49))" two_days_ago
"$XTOUCH" -at "@$((now - 60 * 60 * 25))" yesterday
"$XTOUCH" -at "@$((now - 60 * 60))" one_hour_ago
"$XTOUCH" -at "@$((now))" now
"$XTOUCH" -at "@$((now + 60 * 60 * 24))" tomorrow

bfs_diff . -mindepth 1 \
    \( -atime -1 -exec printf -- '-atime -1: %s\n' {} \; -o -true \) \
    \( -atime  1 -exec printf -- '-atime  1: %s\n' {} \; -o -true \) \
    \( -atime +1 -exec printf -- '-atime +1: %s\n' {} \; -o -true \)
