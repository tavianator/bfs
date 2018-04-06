`bfs`
=====

[![License](http://img.shields.io/badge/license-0BSD-blue.svg)](https://github.com/tavianator/bfs/blob/master/COPYING)
[![LOC](https://tokei.rs/b1/github/tavianator/bfs?category=code)](https://github.com/Aaronepower/tokei)
[![Build Status](https://api.travis-ci.org/tavianator/bfs.svg?branch=master)](https://travis-ci.org/tavianator/bfs)

Breadth-first search for your files.

`bfs` is a variant of the UNIX `find` command that operates [breadth-first](https://en.wikipedia.org/wiki/Breadth-first_search) rather than [depth-first](https://en.wikipedia.org/wiki/Depth-first_search).
It is otherwise intended to be compatible with many versions of `find`, including

- [POSIX `find`](http://pubs.opengroup.org/onlinepubs/9699919799/utilities/find.html)
- [GNU `find`](https://www.gnu.org/software/findutils/)
- {[Free](https://www.freebsd.org/cgi/man.cgi?find(1)),[Open](https://man.openbsd.org/find.1),[Net](http://netbsd.gw.com/cgi-bin/man-cgi?find+1+NetBSD-current)}BSD `find`
- [macOS `find`](https://developer.apple.com/legacy/library/documentation/Darwin/Reference/ManPages/man1/find.1.html)

If you're not familiar with `find`, the [GNU find manual](https://www.gnu.org/software/findutils/manual/html_mono/find.html) provides a good introduction.


Breadth vs. depth
-----------------

The advantage of breadth-first over depth first search is that it usually finds the file(s) you're looking for faster.
Imagine the following directory tree:

<pre>
haystack
├── deep
│   └── 1
│       └── 2
│           └── 3
│               └── 4
│                   └── ...
└── shallow
    └── <strong>needle</strong>
</pre>

`find` will explore the entire `deep` directory tree before it ever gets to the `shallow` one that contains what you're looking for.

<pre>
$ <strong>find</strong> haystack
haystack
haystack/deep
haystack/deep/1
haystack/deep/1/2
haystack/deep/1/2/3
haystack/deep/1/2/3/4
...
haystack/shallow
<strong>haystack/shallow/needle</strong>
</pre>

On the other hand, `bfs` lists files from shallowest to deepest, so you never have to wait for it to explore an entire unrelated subtree.

<pre>
$ <strong>bfs</strong> haystack
haystack
haystack/deep
haystack/shallow
haystack/deep/1
<strong>haystack/shallow/needle</strong>
haystack/deep/1/2
haystack/deep/1/2/3
haystack/deep/1/2/3/4
...
</pre>


Easy
----

`bfs` tries to be easier to use than `find`, while remaining compatible.
For example, `bfs` is less picky about where you put its arguments:

<pre>
$ <strong>find</strong> -L -name 'needle' <em>haystack</em>
find: paths must precede expression: haystack
$ <strong>bfs</strong> -L -name 'needle' <em>haystack</em>
<strong>haystack/needle</strong>

$ <strong>find</strong> <em>haystack</em> -L -name 'needle'
find: unknown predicate `-L'
$ <strong>bfs</strong> <em>haystack</em> -L -name 'needle'
<strong>haystack/needle</strong>

$ <strong>find</strong> -L <em>haystack</em> -name 'needle'
<strong>haystack/needle</strong>
$ <strong>bfs</strong> -L <em>haystack</em> -name 'needle'
<strong>haystack/needle</strong>
</pre>

`bfs` also adds some extra options that make some common tasks easier.
Compare `bfs -nohidden` to

    find -name '.?*' -prune -o -print


Pretty
------

When `bfs` detects that its output is a terminal, it automatically colors its output with the same colors `ls` uses.
This makes it easier to identify relevant files at a glance.

<img src="https://tavianator.github.io/bfs/screenshot.svg" alt="Screenshot" width="100%" />


Try it!
-------

To get `bfs`, download one of the [releases](https://github.com/tavianator/bfs/releases) or clone the [git repo](https://github.com/tavianator/bfs).
Then run

    $ make

This will build the `bfs` binary in the current directory.
You can test it out:

    $ ./bfs -nohidden

If you're interested in speed, you may want to build the release version instead:

    $ make clean
    $ make release

Finally, if you want to install it globally, run

    $ sudo make install

Alternatively, `bfs` may already be packaged for your distribution of choice:

[![Packaging status](https://repology.org/badge/vertical-allrepos/bfs.svg)](https://repology.org/metapackage/bfs)

For example:

    # apt install bfs                  # Debian/Ubuntu
    # brew install tavianator/tap/bfs  # macOS Homebrew
