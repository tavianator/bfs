0.*
===


0.82
----

**Unreleased**

62/76 GNU find features supported.

- Color files with multiple hard links correctly
- At `-O3`, replace command lines with no action with `-false`
  - For example, `bfs -true -o -print` becomes `-false`, because `-print` is not reachable
- Move optimizations that rely on the purity of a predicate to `-O2` (the new default)
- Optimize using De Morgan's laws:
  - `( ! X -a ! Y ) <==> ! ( X -o Y )`
  - `( ! X -o ! Y ) <==> ! ( X -a Y )`
  - `! ( X -a ! Y ) <==> ( ! X -o Y )`
  - `! ( X -o ! Y ) <==> ( ! X -a Y )`
  - `! ( ! X -a Y ) <==> ( X -o ! Y )`
  - `! ( ! X -o Y ) <==> ( X -a ! Y )`
- At `-O2`, remove top-level pure expressions
  - For example, `bfs -print , -false` becomes `-print`, because the top-level return value is ignored
- At `-O2`, remove negations from the left-hand side of the comma operator
  - For example, `-not -print , -print` becomes `-print , -print`, because the return value of the LHS is ignored
- Treat `-`, `)`, and `,` as paths when required to by POSIX
  - `)` and `,` are only supported before the expression begins
- Implement `-D opt`
- Implement `-fprint`
- Implement `-fprint0`


0.79
----

**May 27, 2016**

60/76 GNU find features supported.

- Remove an errant debug `printf()` from `-used`
- Implement the `{} ;` variants of `-exec`, `-execdir`, `-ok`, and `-okdir`


0.74
----

**March 12, 2016**

56/76 GNU find features supported.

- Color broken symlinks correctly
- Fix https://github.com/tavianator/bfs/issues/7
- Fix `-daystart`'s rounding of midnight
- Implement (most of) `-newerXY`
- Implement `-used`
- Implement `-size`


0.70
----

**February 23, 2016**

53/76 GNU find features supported.

- New `make install` and `make uninstall` targets
- Squelch non-positional warnings for `-follow`
- Reduce memory footprint by as much as 64% by closing `DIR*`s earlier
- Speed up `bfs` by ~5% by using a better FD cache eviction policy
- Fix infinite recursion when evaluating `! expr`
- Optimize unused pure expressions (e.g. `-empty -a -false`)
- Optimize double-negation (e.g. `! ! -name foo`)
- Implement `-D stat` and `-D tree`
- Implement `-O`


0.67
----

**February 14, 2016**

Initial release.

51/76 GNU find features supported.
