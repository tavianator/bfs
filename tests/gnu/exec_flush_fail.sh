# Failure to flush streams before exec should be caught
skip_unless test -e /dev/full
fail invoke_bfs basic -print0 -exec true \; >/dev/full
