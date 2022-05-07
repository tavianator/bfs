<div align="center">

`bfs`
=====

<a href="https://github.com/tavianator/bfs/releases"><img src="https://img.shields.io/github/v/tag/tavianator/bfs?label=version" alt="Version" align="left"></a>
<a href="/LICENSE"><img src="https://img.shields.io/badge/license-0BSD-blue.svg" alt="License" align="left"></a>
<a href="https://github.com/tavianator/bfs/actions/workflows/ci.yml"><img src="https://img.shields.io/github/workflow/status/tavianator/bfs/CI?label=CI" alt="CI Status" align="right"></a>
<a href="https://codecov.io/gh/tavianator/bfs"><img src="https://img.shields.io/codecov/c/github/tavianator/bfs?token=PpBVuozOVC" alt="Code coverage" align="right"/></a>

***Breadth-first search for your files.***

[ **[Features](#features)** ]&emsp;
[ **[Installation](#installation)** ]&emsp;
[ **[Changelog](/docs/CHANGELOG.md)** ]&emsp;
[ **[Building](/docs/BUILDING.md)** ]&emsp;
[ **[Hacking](/docs/HACKING.md)** ]

<img src="https://tavianator.github.io/bfs/animation.svg" alt="Screenshot">
<p></p>
</div>

`bfs` is a variant of the UNIX `find` command that operates [**breadth-first**](https://en.wikipedia.org/wiki/Breadth-first_search) rather than [**depth-first**](https://en.wikipedia.org/wiki/Depth-first_search).
It is otherwise compatible with many versions of `find`, including

<div align="center">

[ **[POSIX](http://pubs.opengroup.org/onlinepubs/9699919799/utilities/find.html)** ]&emsp;
[ **[GNU](https://www.gnu.org/software/findutils/)** ]&emsp;
[ **[FreeBSD](https://www.freebsd.org/cgi/man.cgi?find(1))** ]&emsp;
[ **[OpenBSD](https://man.openbsd.org/find.1)** ]&emsp;
[ **[NetBSD](https://man.netbsd.org/find.1)** ]&emsp;
[ **[macOS](https://ss64.com/osx/find.html)** ]

</div>

If you're not familiar with `find`, the [GNU find manual](https://www.gnu.org/software/findutils/manual/html_mono/find.html) provides a good introduction.


Features
--------

<details>
<summary>
<code>bfs</code> operates breadth-first, which typically finds the file(s) you're looking for faster.
<p></p>
</summary>

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
<summary>
<code>bfs</code> tries to be easier to use than <code>find</code>, while remaining compatible.
<p></p>
</summary>

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
<summary>
<code>bfs</code> gives helpful errors and warnings.
<p></p>
</summary>

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
<summary>
<code>bfs</code> adds some options that make common tasks easier.
<p></p>
</summary>

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


Installation
------------

<details open>
<summary>
<code>bfs</code> may already be packaged for your operating system.
<p></p>
</summary>

<pre>
<strong>Alpine Linux</strong>
# apk add bfs

<strong>Arch Linux</strong>
Available in the <a href="https://aur.archlinux.org/packages/bfs">AUR</a>

<strong>Debian/Ubuntu</strong>
# apt install bfs

<strong>NixOS</strong>
# nix-env -i bfs

<strong>Void Linux</strong>
# xbps-install -S bfs

<strong>FreeBSD</strong>
# pkg install bfs

<strong>MacPorts</strong>
# port install bfs

<strong>Homebrew</strong>
$ brew install tavianator/tap/bfs
</pre>
</details>

<details>
<summary>
To build <code>bfs</code> from source, you may need to install some dependencies.
<p></p>
</summary>

The only absolute requirements for building `bfs` are a C compiler, [GNU make](https://www.gnu.org/software/make/), and [Bash](https://www.gnu.org/software/bash/).
These are installed by default on many systems, and easy to install on most others.
Refer to your operating system's documentation on building software.

`bfs` also depends on some system libraries for some of its features.
These dependencies are optional, and can be turned off at build time if necessary by setting the appropriate variable to the empty string (e.g. `make WITH_ONIGURUMA=`).

| Dependency                                            | Platforms  | `make` flag      |
|-------------------------------------------------------|------------|------------------|
| [acl](https://savannah.nongnu.org/projects/acl)       | Linux only | `WITH_ACL`       |
| [attr](https://savannah.nongnu.org/projects/attr)     | Linux only | `WITH_ATTR`      |
| [libcap](https://sites.google.com/site/fullycapable/) | Linux only | `WITH_LIBCAP`    |
| [Oniguruma](https://github.com/kkos/oniguruma)        | All        | `WITH_ONIGURUMA` |

Here's how to install them on some common platforms:

<pre>
<strong>Alpine Linux</strong>
# apk add acl{,-dev} attr{,-dev} libcap{,-dev} oniguruma-dev

<strong>Arch Linux</strong>
# pacman -S acl attr libcap oniguruma

<strong>Debian/Ubuntu</strong>
# apt install acl libacl1-dev attr libattr1-dev libcap2-bin libcap-dev libonig-dev

<strong>Fedora</strong>
# dnf install libacl-devel libattr-devel libcap-devel oniguruma-devel

<strong>NixOS</strong>
# nix-env -i acl attr libcap oniguruma

<strong>Void Linux</strong>
# xbps-install -S acl-{devel,progs} attr-{devel,progs} libcap-{devel,progs} oniguruma-devel

<strong>FreeBSD</strong>
# pkg install oniguruma

<strong>MacPorts</strong>
# port install oniguruma6

<strong>Homebrew</strong>
$ brew install oniguruma
</pre>
</details>

<details>
<summary>
Once you have the dependencies, you can build <code>bfs</code>.
<p></p>
</summary>

Download one of the [releases](https://github.com/tavianator/bfs/releases) or clone the [git repo](https://github.com/tavianator/bfs).
Then run

    $ make

This will build the `bfs` binary in the current directory.
Run the test suite to make sure it works correctly:

    $ make check

If you're interested in speed, you may want to build the release version instead:

    $ make release

Finally, if you want to install it globally, run

    # make install

</details>
