
# Usage

<br>

## Options

*Command - line options **BFS** provides.*

<br>

### Exclude

Skips matching items.

```sh
bfs -name config -exclude -name .git
```

🠖 Skips any files / directories named `.git`

<br>

*Can even be used with `-depth` & `-delete` .*

<br>

### Hidden

In / Excludes `.files`

```sh
bfs -name test -nohidden    # Excludes
```

```sh
bfs -name test -hidden      # Includes
```

<br>

### Unique

Ensures every file is only visited once even if it's <br>
reachable through multiple hard / symbolic links.

```sh
bfs -name images -L -unique
```

*Particularly useful when following symbolic links (* `-L` *)*

<br>

### Color

**BFS** automatically colors paths similar to **GNUs** `ls`. <br>
*For this it uses the* `LS_COLORS` *environment variable.*

```sh
bfs -nocolor -name documents
```

This option might come into handy if you want <br>
to preserve the original colors through a pipe:

```sh
bfs -color | less -R
```

*If the* [`NO_COLOR`][Color] *environment variable <br>
is set, coloring will be disabled by default.*

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

<br>

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

<!----------------------------------------------------------------------------->

[Color]: https://no-color.org/

