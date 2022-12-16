# Failure to flush streams before exec should be caught
test -e /dev/full || skip
fail invoke_bfs basic -print0 -exec true \; >/dev/full
