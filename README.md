<div align="center">

<h1>
<code>bfs</code>
<br clear="all">
<a href="https://github.com/tavianator/bfs/releases"><img src="https://img.shields.io/github/v/tag/tavianator/bfs?label=version" alt="Version" align="left"></a>
<a href="/LICENSE"><img src="https://img.shields.io/badge/license-0BSD-blue.svg" alt="License" align="left"></a>
<a href="https://github.com/tavianator/bfs/actions/workflows/ci.yml"><img src="https://github.com/tavianator/bfs/actions/workflows/ci.yml/badge.svg" alt="CI Status" align="right"></a>
<a href="https://codecov.io/gh/tavianator/bfs"><img src="https://img.shields.io/codecov/c/github/tavianator/bfs?token=PpBVuozOVC" alt="Code coverage" align="right"/></a>
</h1>

**[Features]   •   [Installation]   •   [Usage]   •   [Building]   •   [Contributing]   •   [Changelog]**

[Features]: #features
[Installation]: #installation
[Usage]: /docs/USAGE.md
[Building]: /docs/BUILDING.md
[Contributing]: /docs/CONTRIBUTING.md
[Changelog]: /docs/CHANGELOG.md

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/tavianator/bfs/gh-pages/animation-dark.svg">
  <source media="(prefers-color-scheme: light)" srcset="https://raw.githubusercontent.com/tavianator/bfs/gh-pages/animation-light.svg">
  <img alt="Screencast" src="https://raw.githubusercontent.com/tavianator/bfs/gh-pages/animation-light.svg">
</picture>
<p></p>

</div>

`bfs` is a variant of the UNIX `find` command that operates [**breadth-first**](https://en.wikipedia.org/wiki/Breadth-first_search) rather than [**depth-first**](https://en.wikipedia.org/wiki/Depth-first_search).
It is otherwise compatible with many versions of `find`, including

<div align="center">

**[POSIX]   •   [GNU]   •   [FreeBSD]   •   [OpenBSD]   •   [NetBSD]   •   [macOS]**

[POSIX]: http://pubs.opengroup.org/onlinepubs/9699919799/utilities/find.html
[GNU]: https://www.gnu.org/software/findutils/
[FreeBSD]: https://www.freebsd.org/cgi/man.cgi?find(1)
[OpenBSD]: https://man.openbsd.org/find.1
[NetBSD]: https://man.netbsd.org/find.1
[macOS]: https://ss64.com/osx/find.html

</div>

If you're not familiar with `find`, the [GNU find manual](https://www.gnu.org/software/findutils/manual/html_mono/find.html) provides a good introduction.


Features
--------

<details>
<summary>
<code>bfs</code> operates breadth-first, which typically finds the file(s) you're looking for faster.
</summary>
<p></p>

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
On the other hand, `bfs` lists files from shallowest to deepest, so you never have to wait for it to explore an entire unrelated subtree.

<table>
<tbody>
<tr><th><code>bfs</code></th><th><code>find</code></th></tr>
<tr>
<td width="506" valign="top">

```console
$ bfs haystack
haystack
haystack/deep
haystack/shallow
haystack/deep/1
haystack/shallow/needle
...
```

</td>
<td width="506" valign="top">

```console
$ find haystack
haystack
haystack/deep
haystack/deep/1
haystack/deep/1/2
haystack/deep/1/2/3
haystack/deep/1/2/3/4
...
haystack/shallow
haystack/shallow/needle
```

</td>
</tr>
</tbody>
</table>
</details>

<details>
<summary>
<code>bfs</code> tries to be easier to use than <code>find</code>, while remaining compatible.
</summary>
<p></p>

For example, `bfs` is less picky about where you put its arguments:

<table>
<tbody>
<tr><th><code>bfs</code></th><th><code>find</code></th></tr>
<tr>
<td width="506">

```console
$ bfs -L -name 'needle' haystack
haystack/needle

$ bfs haystack -L -name 'needle'
haystack/needle

$ bfs -L haystack -name 'needle'
haystack/needle
```

</td>
<td width="506">

```console
$ find -L -name 'needle' haystack
find: paths must precede expression: haystack

$ find haystack -L -name 'needle'
find: unknown predicate `-L'

$ find -L haystack -name 'needle'
haystack/needle
```

</td>
</tr>
</tbody>
</table>
</details>

<details>
<summary>
<code>bfs</code> gives helpful errors and warnings.
</summary>
<p></p>

For example, `bfs` will detect and suggest corrections for typos:

```console
$ bfs -nam needle
bfs: error: bfs -nam needle
bfs: error:     ~~~~
bfs: error: Unknown argument; did you mean -name?
```

`bfs` also includes a powerful static analysis to help catch mistakes:

```console
$ bfs -print -name 'needle'
bfs: warning: bfs -print -name needle
bfs: warning:            ~~~~~~~~~~~~
bfs: warning: The result of this expression is ignored.
```

</details>

<details>
<summary>
<code>bfs</code> adds some options that make common tasks easier.
</summary>
<p></p>

For example, the `-exclude` operator skips over entire subtrees whenever an expression matches.
`-exclude` is both more powerful and easier to use than the standard `-prune` action; compare

<pre>
$ bfs -name config <strong>-exclude -name .git</strong>
</pre>

to the equivalent

<pre>
$ find <strong>! \( -name .git -prune \)</strong> -name config
</pre>

As an additional shorthand, `-nohidden` skips over all hidden files and directories.
See the [usage documentation](/docs/USAGE.md#extensions) for more about the extensions provided by `bfs`.
</details>


Installation
------------

<details open>
<summary>
<code>bfs</code> may already be packaged for your operating system.
</summary>
<p></p>

<table>
<tbody>
<tr><th>Linux</th><th>macOS</th></tr>

<tr>
<td width="506" valign="top" rowspan="3">

<pre>
<strong><a href="https://pkgs.alpinelinux.org/packages?name=bfs">Alpine Linux</a></strong>
# apk add bfs

<strong><a href="https://archlinux.org/packages/extra/x86_64/bfs/">Arch Linux</a></strong>
# pacman -S bfs

<strong><a href="https://packages.debian.org/sid/bfs">Debian</a>/<a href="https://packages.ubuntu.com/kinetic/bfs">Ubuntu</a></strong>
# apt install bfs

<strong><a href="https://src.fedoraproject.org/rpms/bfs">Fedora Linux</a></strong>
# dnf install bfs

<strong><a href="https://packages.guix.gnu.org/packages/bfs/">GNU Guix</a></strong>
# guix install bfs

<strong><a href="https://search.nixos.org/packages?channel=unstable&show=bfs&from=0&size=1&sort=relevance&type=packages&query=bfs">NixOS</a></strong>
# nix-env -i bfs

<strong><a href="https://voidlinux.org/packages/?arch=x86_64&q=bfs">Void Linux</a></strong>
# xbps-install -S bfs
</pre>

</td>
<td width="506" valign="top">

<pre>
<strong><a href="https://formulae.brew.sh/formula/bfs">Homebrew</a></strong>
$ brew install bfs

<strong><a href="https://ports.macports.org/port/bfs/">MacPorts</a></strong>
# port install bfs
</pre>

</td>
</tr>
<tr><th height="1">BSD</th></tr>
<tr>
<td width="506" valign="top">

<pre>
<strong><a href="https://www.freshports.org/sysutils/bfs">FreeBSD</a></strong>
# pkg install bfs

<strong><a href="https://openports.pl/path/sysutils/bfs">OpenBSD</a></strong>
# pkg_add bfs
</pre>

</td>
</tr>
</tbody>
</table>
</details>

<details>
<summary>
To build <code>bfs</code> from source, you may need to install some dependencies.
</summary>
<p></p>

The only absolute requirements for building `bfs` are a C compiler, [GNU make](https://www.gnu.org/software/make/), and [Bash](https://www.gnu.org/software/bash/).
These are installed by default on many systems, and easy to install on most others.
Refer to your operating system's documentation on building software.

`bfs` also depends on some system libraries for some of its features.
Here's how to install them on some common platforms:

<pre>
<strong>Alpine Linux</strong>
# apk add acl{,-dev} attr{,-dev} libcap{,-dev} liburing-dev oniguruma-dev

<strong>Arch Linux</strong>
# pacman -S acl attr libcap liburing oniguruma

<strong>Debian/Ubuntu</strong>
# apt install acl libacl1-dev attr libattr1-dev libcap2-bin libcap-dev liburing-dev libonig-dev

<strong>Fedora</strong>
# dnf install acl libacl-devel libattr-devel libcap-devel liburing-devel oniguruma-devel

<strong>NixOS</strong>
# nix-env -i acl attr libcap liburing oniguruma

<strong>Void Linux</strong>
# xbps-install -S acl-{devel,progs} attr-{devel,progs} libcap-{devel,progs} liburing-devel oniguruma-devel

<strong>Homebrew</strong>
$ brew install oniguruma

<strong>MacPorts</strong>
# port install oniguruma6

<strong>FreeBSD</strong>
# pkg install oniguruma
</pre>

These dependencies are technically optional, though strongly recommended.
See the [build documentation](/docs/BUILDING.md#dependencies) for how to disable them.
</details>

<details>
<summary>
Once you have the dependencies, you can build <code>bfs</code>.
</summary>
<p></p>

Download one of the [releases](https://github.com/tavianator/bfs/releases) or clone the [git repo](https://github.com/tavianator/bfs).
Then run

    $ make

This will build the `./bin/bfs` binary.
Run the test suite to make sure it works correctly:

    $ make check

If you're interested in speed, you may want to build the release version instead:

    $ make release

Finally, if you want to install it globally, run

    # make install

</details>
