printf 'basic/c\0' >"$TEST/in1"
printf 'basic/g\0' >"$TEST/in2"
bfs_diff -files0-from "$TEST/in1" -files0-from "$TEST/in2"
