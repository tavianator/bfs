cd "$TEST" || exit

invoke_bfs -quit -ignore_vcs || skip

# Require git for repository setup
command -v git >/dev/null 2>&1 || skip

(
    mkdir subrepo
    cd subrepo || fail
    git init -q
    git config user.email "a@a.com" >/dev/null
    git config user.name "a" >/dev/null
    echo subrepo-ignore > .gitignore
    "$XTOUCH" subrepo-keep
    git add .; git commit -q -m "abc"
)

(
    mkdir repo
    cd repo || fail

    git init -q
    echo "outer-ignore" > .gitignore
    "$XTOUCH" outer-ignore outer-keep

    env GIT_ALLOW_PROTOCOL=file git submodule add -q ../subrepo subrepo

    echo "ignored_repo" >> .gitignore
    mkdir ignored_repo
    cd ignored_repo || fail
    git init -q
    "$XTOUCH" file
    cd ..

    mkdir dir_content_ignore
    echo '/*' > dir_content_ignore/.gitignore
    "$XTOUCH" dir_content_ignore/file
)

"$XTOUCH" outer-keep

bfs_diff . -ignore_vcs -mindepth 1 -print
