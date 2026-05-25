# Regression test: times that overflow localtime() should still print
cd "$TEST"
"$XTOUCH" -t "@1111111111111111111" overflow || skip
invoke_bfs . -fls "$OUT"
