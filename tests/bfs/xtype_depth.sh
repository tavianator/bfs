cd "$TEST"
"$XTOUCH" -p foo/bar
ln -s foo/bar baz

chmod a-rx foo
defer chmod +rx foo

# Make sure -xtype is considered side-effecting for facts_when_impure
! invoke_bfs . -xtype l -depth 100
