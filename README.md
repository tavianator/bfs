`bfs`
=====

[![License](http://img.shields.io/badge/license-0BSD-blue.svg)](https://github.com/tavianator/bfs/blob/main/LICENSE)
[![Version](https://img.shields.io/github/v/tag/tavianator/bfs?label=version)](https://github.com/tavianator/bfs/releases)
[![CI Status](https://github.com/tavianator/bfs/actions/workflows/ci.yml/badge.svg)](https://github.com/tavianator/bfs/actions/workflows/ci.yml)

Breadth-first search for your files.

<img src="https://tavianator.github.io/bfs/animation.svg" alt="Screenshot" />

`bfs` is a variant of the UNIX `find` command that operates [breadth-first](https://en.wikipedia.org/wiki/Breadth-first_search) rather than [depth-first](https://en.wikipedia.org/wiki/Depth-first_search).
It is otherwise compatible with many versions of `find`, including

- [POSIX `find`](http://pubs.opengroup.org/onlinepubs/9699919799/utilities/find.html)
- [GNU `find`](https://www.gnu.org/software/findutils/)
- {[Free](https://www.freebsd.org/cgi/man.cgi?find(1)),[Open](https://man.openbsd.org/find.1),[Net](https://man.netbsd.org/find.1)}BSD `find`
- [macOS `find`](https://ss64.com/osx/find.html)

If you're not familiar with `find`, the [GNU find manual](https://www.gnu.org/software/findutils/manual/html_mono/find.html) provides a good introduction.


Features
--------

<details>
<summary><code>bfs</code> operates breadth-first, which typically finds the file(s) you're looking for faster.</summary>

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
</details>

<details>
<summary><code>bfs</code> tries to be easier to use than <code>find</code>, while remaining compatible.</summary>

For example, `bfs` is less picky about where you put its arguments:

<pre>
$ <strong>bfs</strong> -L -name 'needle' <em>haystack</em>    │ $ <strong>find</strong> -L -name 'needle' <em>haystack</em>
<strong>haystack/needle</strong>                     │ find: paths must precede expression: haystack
                                    │
$ <strong>bfs</strong> <em>haystack</em> -L -name 'needle'    │ $ <strong>find</strong> <em>haystack</em> -L -name 'needle'
<strong>haystack/needle</strong>                     │ find: unknown predicate `-L'
                                    │
$ <strong>bfs</strong> -L <em>haystack</em> -name 'needle'    │ $ <strong>find</strong> -L <em>haystack</em> -name 'needle'
<strong>haystack/needle</strong>                     │ <strong>haystack/needle</strong>
</pre>
</details>

<details>
<summary><code>bfs</code> gives helpful errors and warnings.</summary>

For example, `bfs` will detect and suggest corrections for typos:

<pre>
$ bfs -nam needle
<strong>bfs: error:</strong> bfs <strong>-nam</strong> needle
<strong>bfs: error:</strong>     <strong>~~~~</strong>
<strong>bfs: error:</strong> Unknown argument; did you mean <strong>-name</strong>?
</pre>

`bfs` also includes a powerful static analysis to identify likely mistakes:

<pre>
$ bfs -print -name 'needle'
<strong>bfs: warning:</strong> bfs -print <strong>-name needle</strong>
<strong>bfs: warning:</strong>            <strong>~~~~~~~~~~~~</strong>
<strong>bfs: warning:</strong> The result of this expression is ignored.
</pre>
</details>

<details>
<summary><code>bfs</code> adds some options that make common tasks easier.</summary>

### `-exclude`

The `-exclude` operator skips an entire subtree whenever an expression matches.
For example, `-exclude -name .git` will exclude any files or directories named `.git` from the search results.
`-exclude` is easier to use than the standard `-prune` action; compare

    bfs -name config -exclude -name .git

to the equivalent

    find ! \( -name .git -prune \) -name config

Unlike `-prune`, `-exclude` even works in combination with `-depth`/`-delete`.

---

### `-hidden`/`-nohidden`

`-hidden` matches "hidden" files (dotfiles).
`bfs -hidden` is effectively shorthand for

    find \( -name '.*' -not -name . -not -name .. \)

`-nohidden` is equivalent to `-exclude -hidden`.

---

### `-unique`

This option ensures that `bfs` only visits each file once, even if it's reachable through multiple hard or symbolic links.
It's particularly useful when following symbolic links (`-L`).

---

### `-color`/`-nocolor`

When printing to a terminal, `bfs` automatically colors paths like GNU `ls`, according to the `LS_COLORS` environment variable.
The `-color` and `-nocolor` options override the automatic behavior, which may be handy when you want to preserve colors through a pipe:

    bfs -color | less -R

If the [`NO_COLOR`](https://no-color.org/) environment variable is set, colors will be disabled by default.
</details>


