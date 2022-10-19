touchp scratch/foo.out
ln scratch/foo.out scratch/foo.hard
ln -s foo.out scratch/foo.soft

invoke_bfs basic -fprint scratch/foo.out -fprint scratch/foo.hard -fprint scratch/foo.soft
sort scratch/foo.out >"$OUT"
diff_output
