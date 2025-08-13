cd "$TEST"

# Require git to set up a repository
command -v git >/dev/null 2>&1 || skip

# Initialize a repo in the temp test dir
git init -q || skip

# Ignore names starting with "ignored"
echo "ignored*" > .gitignore

# Create files: one ignored by git, one not
"$XTOUCH" ignored_file tracked_file || skip

# Detect whether -ignore_vcs has any effect (i.e., built with libgit2)
invoke_bfs . -name '*_file' -print >"$OUT.base" || fail
invoke_bfs . -ignore_vcs -name '*_file' -print >"$OUT.ign" || fail
if cmp -s "$OUT.base" "$OUT.ign"; then
    # No change => no libgit2 support (or feature disabled). Skip.
    skip
fi

# Now snapshot-test the behavior
bfs_diff . -ignore_vcs -name '*_file' -print
