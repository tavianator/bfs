0.*
===


0.82
----

**Unreleased**

62/76 GNU find features supported.

- Rework optimization levels
  - `-O1` (logical simplification):
    - Constant propagation (`! -false <==> -true`, `! -true <==> -false`)
    - Double negation (`! ! X <==> X`)
    - Conjunction elimination:
      - `-true -a X <==> X`
      - `X -a -true <==> X`
    - Disjunctive syllogism:
      - `-false -o X <==> X`
      - `X -o -false <==> X`
    - Short-circuiting:
      - `-false -a X <==> -false`
      - `-true -o X <==> -true`
    - De Morgan's laws (**new**):
      - `( ! X -a ! Y ) <==> ! ( X -o Y )`
      - `( ! X -o ! Y ) <==> ! ( X -a Y )`
      - `! ( X -a ! Y ) <==> ( ! X -o Y )`
      - `! ( X -o ! Y ) <==> ( ! X -a Y )`
      - `! ( ! X -a Y ) <==> ( X -o ! Y )`
      - `! ( ! X -o Y ) <==> ( X -a ! Y )`
    - Unused result (**new**): `! X , Y <==> X , Y`
  - `-O2` (purity):
    - (These optimizations take the purity of predicates into account, allowing side-effect-free tests like `-name` or `-type` to be moved or removed)
    - `PURE -a -false <==> -false`
    - `PURE -o -true <==> -true`
    - `PURE , X <==> X`
    - Top-level unused result (**new**): `X (-a|-o|,) PURE <==> X`
  - `-O3` (cost-based, **default**):
    - Re-order tests to reduce the expected cost (TODO)
  - `-O4` (aggressive):
    - (These are very aggressive optimizations that may have surprising effects on warning/error messages and runtime, but still should not affect the resulting output)
    - Change top-level expressions with no actions to `-false` (**new**):
      - For example, `bfs -O4 -true -o -print` becomes `-false`, because `-print` is unreachable
    - Skip the entire traversal if the top-level expression is `-false`
      - `bfs -O4 -false` (or anything that optimizes to `-false`) will exit immediately
      - This may cause messages about non-existent files, symbolic link cycles, etc. to be skipped
  - `-Ofast`:
    - Always the highest level, currently the same as `-O4`
- Color files with multiple hard links correctly
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
