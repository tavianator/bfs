cd "$TEST"

now=$(epoch_time)

# -used is always false if atime < ctime
"$XTOUCH" -at "@$((now - 60 * 60 * 24))" yesterday

# -used rounds up
"$XTOUCH" -at "@$((now + 60 * 60))" tomorrow

"$XTOUCH" -at "@$((now + 60 * 60 * 25))" dayafter

"$XTOUCH" -at "@$((now + 60 * 60 * (24 * 6 + 1)))" nextweek

"$XTOUCH" -at "@$((now + 60 * 60 * 24 * 365))" nextyear

bfs_diff -mindepth 1 \
    -a -used 1 -printf '-used 1: %p\n' \
    -o -used 2 -printf '-used 2: %p\n' \
    -o -used 7 -printf '-used 7: %p\n' \
    -o -used +7 -printf '-used +7: %p\n'
