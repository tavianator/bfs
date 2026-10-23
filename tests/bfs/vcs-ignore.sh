cd "$TEST" || exit

invoke_bfs -quit -ignore_vcs || skip

# Require git to set up a repository
command -v git >/dev/null 2>&1 || skip

# Initialize a repo in the temp test dir
git init -q

# Ignore names starting with "ignored"
echo "ignored*" > .gitignore

mkdir ignored_dir tracked_dir ignored_dir_empty
# Create files: one ignored by git, one not, one in ignored dir
"$XTOUCH" ignored_file tracked_file ignored_dir/file

# Now snapshot-test the behavior
bfs_diff . -mindepth 1 -ignore_vcs -print
