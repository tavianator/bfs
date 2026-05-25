PATH="foo:$PATH" bfs_diff basic -execdir /bin/sh -c 'printf "%s\\n" "$@"' sh {} +
