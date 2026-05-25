cd "$TEST"

now=$(epoch_time)

"$XTOUCH" -at "@$((now - 60 * 60))" one_hour_ago
"$XTOUCH" -at "@$((now - 121))" two_minutes_ago
"$XTOUCH" -at "@$((now - 61))" one_minute_ago
"$XTOUCH" -at "@$((now - 30))" thirty_seconds_ago
"$XTOUCH" -at "@$((now + 60))" in_one_minute
"$XTOUCH" -at "@$((now + 60 * 60))" in_one_hour

bfs_diff . -mindepth 1 \
    \( -amin -1 -exec printf -- '-amin -1: %s\n' {} \; -o -true \) \
    \( -amin  1 -exec printf -- '-amin  1: %s\n' {} \; -o -true \) \
    \( -amin +1 -exec printf -- '-amin +1: %s\n' {} \; -o -true \)
