cd "$TEST" || exit

invoke_bfs -quit -ignore_vcs || skip

command -v git >/dev/null 2>&1 || skip

for repo in repo1 repo2; do
	mkdir "$repo" || skip
	cd "$repo" || skip

	git init -q || skip
	echo "ignored" > .gitignore
	keep="keep${repo#repo}"
	"$XTOUCH" ignored "$keep" || skip

	cd ..
done

bfs_diff repo1 repo2 -ignore_vcs -print
