invoke_bfs -help | grep -E '\{...?\}' && return 1
invoke_bfs -D help | grep -E '\{...?\}' && return 1
invoke_bfs -S help | grep -E '\{...?\}' && return 1
invoke_bfs -regextype help | grep -E '\{...?\}' && return 1

return 0
