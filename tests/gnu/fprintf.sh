invoke_bfs basic -fprintf "$OUT" '%%p(%p) %%d(%d) %%f(%f) %%h(%h) %%H(%H) %%P(%P) %%m(%m) %%M(%M) %%y(%y)\n'
sort_output
diff_output
