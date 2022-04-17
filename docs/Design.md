
# Design

<br>

## Breadth First

Imagine the following directory tree:

<br>

```
haystack
├── deep
│   └── 1
│       └── 2
│           └── 3
│               └── 4
│                   └── ...
└── shallow
    └── needle
```

<br>

`find` will explore the entire `/deep/` directory <br>
tree before it ever gets to the `/shallow/` one <br>
that contains what you're looking for.

<br>

```console
foo@bar:~$ find haystack
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

<br>

On the other hand, `bfs` lists files from *shallowest* <br>
to *deepest*, so you never have to wait for it to <br>
explore an entire unrelated subtree.

<br>

```console
foo@bar:~$ bfs haystack
haystack
haystack/deep
haystack/shallow
haystack/deep/1
haystack/shallow/needle
haystack/deep/1/2
haystack/deep/1/2/3
haystack/deep/1/2/3/4
...
```
