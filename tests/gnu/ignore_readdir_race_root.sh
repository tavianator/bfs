# Make sure -ignore_readdir_race doesn't suppress ENOENT at the root
fail invoke_bfs basic/nonexistent -ignore_readdir_race
