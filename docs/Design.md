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
