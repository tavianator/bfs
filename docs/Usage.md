

**BFS** operates breath-first, which typically finds files faster.

Imagine the following directory tree:


    haystack
    ├── deep
    │   └── 1
    │       └── 2
    │           └── 3
    │               └── 4
    │                   └── ...
    └── shallow
        └── <strong>needle</strong>


`find` will explore the entire `deep` directory tree before it ever gets to the `shallow` one that contains what you're looking for.

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


On the other hand, `bfs` lists files from shallowest to deepest, so you never have to wait for it to explore an entire unrelated subtree.


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


<br>

---

<br>

## Arguments

**BFS** isn't picky about where you place arguments.

<br>

### Path Last

```console
foo@bar:~$ bfs  -L -name 'needle' haystack
haystack/needle
```
```console
foo@bar:~$ find -L -name 'needle' haystack
find: paths must precede expression: haystack
```

<br>

### Path First

```console
foo@bar:~$ bfs  haystack -L -name 'needle'
haystack/needle
```

```console
foo@bar:~$ find haystack -L -name 'needle'
find: unknown predicate '-L'
```
<br>

### Path Default

```console
foo@bar:~$ bfs  -L haystack -name 'needle'
haystack/needle
```

```console
foo@bar:~$ find -L haystack -name 'needle'
haystack/needle
```

<br>

---

<br>

## Errors & Warnings

### Typos

*Detects & suggests corrections.*

```console
foo@bar:~$ bfs -nam needle
bfs: error: bfs -nam needle
bfs: error:     ~~~~
bfs: error: Unknown argument; did you mean -name?
```

<br>

### Mistakes

*Uses static analysis to identify likely mistakes.*

```console
foo@bar:~$ bfs -print -name 'needle'
bfs: warning: bfs -print -name 'needle'
bfs: warning:            ~~~~~~~~~~~~~~
bfs: warning: The result of this expression is ignored.
```
<br>

---

<br>

# D

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


