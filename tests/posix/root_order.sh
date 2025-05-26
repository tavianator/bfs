# Root paths must be processed in order
# https://www.austingroupbugs.net/view.php?id=1859

# -size forces a stat(), which we don't want to be async
invoke_bfs basic/{a,b,c/d,e/f} -size -1000 >"$OUT"
diff_output
