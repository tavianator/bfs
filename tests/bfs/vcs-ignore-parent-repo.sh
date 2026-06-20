cd "$TEST" || exit 1

invoke_bfs -quit -ignore_vcs || skip

# Require git to set up a repository
command -v git >/dev/null 2>&1 || skip

# Initialize a repo in the temp test dir
git init -q

# Ignore names starting with "ignored"
echo "ignored*" > .gitignore

# subdir and sibling subdir
mkdir -p search_in_dir/level2 search_in_dir/level2_sibling
"$XTOUCH" ignored_file tracked_file \
	search_in_dir/file search_in_dir/ignored_file \
	search_in_dir/level2/file search_in_dir/level2/ignored_file \
	search_in_dir/level2_sibling/file search_in_dir/level2_sibling/ignored_file

# cd into subdir, so the repo root is on parent dir
cd search_in_dir || fail
# Now snapshot-test the behavior
bfs_diff . -mindepth 1 -ignore_vcs -print
