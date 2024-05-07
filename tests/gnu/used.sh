iso8601() {
    printf '%04d-%02d-%02dT%02d:%02d:%02d\n' "$@"
}

cd "$TEST"

now=$(date '+%Y-%m-%dT%H:%M:%S')

# Parse the current date
[[ "$now" =~ ^([0-9]{4})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2}):([0-9]{2})$ ]] || fail
# Treat leading zeros as decimal, not octal
YYYY=$((10#${BASH_REMATCH[1]}))
MM=$((10#${BASH_REMATCH[2]}))
DD=$((10#${BASH_REMATCH[3]}))
hh=$((10#${BASH_REMATCH[4]}))
mm=$((10#${BASH_REMATCH[5]}))
ss=$((10#${BASH_REMATCH[6]}))

# -used is always false if atime < ctime
yesterday=$(iso8601 $YYYY $MM $((DD - 1)) $hh $mm $ss)
"$XTOUCH" -at "$yesterday" yesterday

# -used rounds up
tomorrow=$(iso8601 $YYYY $MM $DD $((hh + 1)) $mm $ss)
"$XTOUCH" -at "$tomorrow" tomorrow

dayafter=$(iso8601 $YYYY $MM $((DD + 1)) $((hh + 1)) $mm $ss)
"$XTOUCH" -at "$dayafter" dayafter

nextweek=$(iso8601 $YYYY $MM $((DD + 6)) $((hh + 1)) $mm $ss)
"$XTOUCH" -at "$nextweek" nextweek

nextyear=$(iso8601 $((YYYY + 1)) $MM $DD $hh $mm $ss)
"$XTOUCH" -at "$nextyear" nextyear

bfs_diff -mindepth 1 \
    -a -used 1 -printf '-used 1: %p\n' \
    -o -used 2 -printf '-used 2: %p\n' \
    -o -used 7 -printf '-used 7: %p\n' \
    -o -used +7 -printf '-used +7: %p\n'
