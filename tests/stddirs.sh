#!/hint/bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

## Standard directory trees for tests

# Creates a simple file+directory structure for tests
make_basic() {
    "$XTOUCH" -p "$1"/{a,b,c/d,e/f,g/h/,i/}
    "$XTOUCH" -p "$1"/{j/foo,k/foo/bar,l/foo/bar/baz}
    echo baz >"$1/l/foo/bar/baz"
}

# Creates a file+directory structure with various permissions for tests
make_perms() {
    "$XTOUCH" -p -M000 "$1/f---------"
    "$XTOUCH" -p -M111 "$1/f--x--x--x"
    "$XTOUCH" -p -M222 "$1/f-w--w--w-"
    "$XTOUCH" -p -M333 "$1/f-wx-wx-wx"
    "$XTOUCH" -p -M444 "$1/fr--r--r--"
    "$XTOUCH" -p -M555 "$1/fr-xr-xr-x" "$1/dr-xr-xr-x/"
    "$XTOUCH" -p -M666 "$1/frw-rw-rw-"
    "$XTOUCH" -p -M777 "$1/frwxrwxrwx" "$1/drwxrwxrwx/"

    "$XTOUCH" -p -M220 "$1/f-w--w----"
    "$XTOUCH" -p -M331 "$1/f-wx-wx--x"
    "$XTOUCH" -p -M664 "$1/frw-rw-r--"
    "$XTOUCH" -p -M775 "$1/frwxrwxr-x" "$1/drwxrwxr-x/"

    "$XTOUCH" -p -M311 "$1/f-wx--x--x"
    "$XTOUCH" -p -M644 "$1/frw-r--r--"
    "$XTOUCH" -p -M755 "$1/frwxr-xr-x" "$1/drwxr-xr-x/"

    "$XTOUCH" -p -M100 "$1/f--x------"
    "$XTOUCH" -p -M200 "$1/f-w-------"
    "$XTOUCH" -p -M300 "$1/f-wx------"
    "$XTOUCH" -p -M400 "$1/fr--------"
    "$XTOUCH" -p -M500 "$1/fr-x------" "$1/dr-x------/"
    "$XTOUCH" -p -M600 "$1/frw-------"
    "$XTOUCH" -p -M700 "$1/frwxr-----" "$1/drwx------/"
}

# Creates a file+directory structure with various symbolic and hard links
make_links() {
    "$XTOUCH" -p "$1/file"
    ln -s file "$1/symlink"
    ln "$1/file" "$1/hardlink"
    ln -s nowhere "$1/broken"
    ln -s symlink/file "$1/notdir"
    "$XTOUCH" -p "$1/deeply/nested"/{dir/,file}
    ln -s file "$1/deeply/nested/link"
    ln -s nowhere "$1/deeply/nested/broken"
    ln -s deeply/nested "$1/skip"
}

# Creates a file+directory structure with symbolic link loops
make_loops() {
    "$XTOUCH" -p "$1/file"
    ln -s file "$1/symlink"
    ln -s nowhere "$1/broken"
    ln -s symlink/file "$1/notdir"
    ln -s loop "$1/loop"
    mkdir -p "$1/deeply/nested/dir"
    ln -s ../../deeply "$1/deeply/nested/loop"
    ln -s deeply/nested/loop/nested "$1/skip"
}

# Creates a file+directory structure with inaccessible files
make_inaccessible() {
    "$XTOUCH" -p -M000 "$1/file" "$1/dir/"
    ln -s dir/file "$1/link"
}

# Creates a file+directory structure with varying timestamps
make_times() {
    "$XTOUCH" -p -t "1991-12-14 00:00" "$1/a"
    "$XTOUCH" -p -t "1991-12-14 00:01" "$1/b"
    "$XTOUCH" -p -t "1991-12-14 00:02" "$1/c"
    ln -s a "$1/l"
    "$XTOUCH" -p -h -t "1991-12-14 00:03" "$1/l"
    "$XTOUCH" -p -t "1991-12-14 00:04" "$1"
}

# Creates a file+directory structure with various weird file/directory names
make_weirdnames() {
    "$XTOUCH" -p "$1/-/a"
    "$XTOUCH" -p "$1/(/b"
    "$XTOUCH" -p "$1/(-/c"
    "$XTOUCH" -p "$1/!/d"
    "$XTOUCH" -p "$1/!-/e"
    "$XTOUCH" -p "$1/,/f"
    "$XTOUCH" -p "$1/)/g"
    "$XTOUCH" -p "$1/.../h"
    "$XTOUCH" -p "$1/\\/i"
    "$XTOUCH" -p "$1/ /j"
    "$XTOUCH" -p "$1/[/k"
    "$XTOUCH" -p "$1/{/l"
    "$XTOUCH" -p "$1/*/m"
    "$XTOUCH" -p "$1/"$'\n/n'
}

# Creates a very deep directory structure for testing PATH_MAX handling
make_deep() {
    mkdir -p "$1"

    # $name will be 255 characters, aka _XOPEN_NAME_MAX
    local name="0123456789ABCDEF"
    name="$name$name$name$name"
    name="$name$name$name$name"
    name="${name:0:255}"

    # 4 * 4 * 256 == 4096 >= PATH_MAX
    local path="$name/$name/$name/$name"
    path="$path/$path/$path/$path"

    "$XTOUCH" -p "$1"/{{0..9},A,B,C,D,E,F}/"$path/$name"
}

# Creates a directory structure with many different types, and therefore colors
make_rainbow() {
    "$XTOUCH" -p "$1/file.txt"
    "$XTOUCH" -p "$1/file.dat"
    "$XTOUCH" -p "$1/lower".{gz,tar,tar.gz}
    "$XTOUCH" -p "$1/upper".{GZ,TAR,TAR.GZ}
    "$XTOUCH" -p "$1/lu.tar.GZ" "$1/ul.TAR.gz"
    ln -s file.txt "$1/link.txt"
    "$XTOUCH" -p "$1/mh1"
    ln "$1/mh1" "$1/mh2"
    mkfifo "$1/pipe"
    # TODO: block
    ln -s /dev/null "$1/chardev_link"
    ln -s nowhere "$1/broken"
    "$MKSOCK" "$1/socket"
    "$XTOUCH" -p "$1"/s{u,g,ug}id
    chmod 06644 "$1"/sugid
    chmod 04644 "$1"/suid
    chmod 02644 "$1"/sgid
    mkdir "$1/ow" "$1"/sticky{,_ow}
    chmod o+w "$1"/*ow
    chmod +t "$1"/sticky*
    "$XTOUCH" -p "$1"/exec.sh
    chmod +x "$1"/exec.sh
    "$XTOUCH" -p "$1/"$'\e[1m/\e[0m'
}

# Create all standard directory trees
make_stddirs() {
    TMP=$(mktemp -d "${TMPDIR:-/tmp}"/bfs.XXXXXXXXXX)

    if ((CLEAN)); then
        defer clean_stddirs
    else
        color printf "Test files saved to ${BLD}%s${RST}\n" "$TMP"
    fi

    chown "$(id -u):$(id -g)" "$TMP"

    make_basic "$TMP/basic"
    make_perms "$TMP/perms"
    make_links "$TMP/links"
    make_loops "$TMP/loops"
    make_inaccessible "$TMP/inaccessible"
    make_times "$TMP/times"
    make_weirdnames "$TMP/weirdnames"
    make_deep "$TMP/deep"
    make_rainbow "$TMP/rainbow"
}

# Clean up temporary directories on exit
clean_stddirs() {
    # Don't force rm to deal with long paths
    for dir in "$TMP"/deep/*/*; do
        if [ -d "$dir" ]; then
            (cd "$dir" && rm -rf *)
        fi
    done

    chmod -R +rwX "$TMP"
    rm -rf "$TMP"
}
