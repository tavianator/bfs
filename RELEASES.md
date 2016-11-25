0.*
===


0.84.1
------

**November 24, 2016**

Bugfix release.

- Fixed https://github.com/tavianator/bfs/issues/7 again
- Like GNU find, don't print warnings by default if standard input is not a terminal
- Redirect standard input from /dev/null for -ok and -okdir
- Skip . when -delete'ing
- Fixed -execdir when the root path has no slashes
- Fixed -execdir in /
- Support -perm +MODE for symbolic modes
- Fixed the build on FreeBSD


0.84
----

**October 29, 2016**

64/76 GNU find features supported.

- Spelling suggestion improvements
- Handle `--`
- (Untested) support for exotic file types like doors, ports, and whiteouts
- Improved robustness in the face of closed std{in,out,err}
- Fixed the build on macOS
- Implement `-ignore_readdir_race`, `-noignore_readdir_race`
- Implement `-perm`


0.82
----

**September 4, 2016**

62/76 GNU find features supported.

- Rework optimization levels
  - `-O1`
    - Simple boolean simplification
  - `-O2`
    - Purity-based optimizations, allowing side-effect-free tests like `-name` or `-type` to be moved or removed
  - `-O3` (**default**):
    - Re-order tests to reduce the expected cost (TODO)
  - `-O4`
    - Aggressive optimizations that may have surprising effects on warning/error messages and runtime, but should not otherwise affect the results
  - `-Ofast`:
    - Always the highest level, currently the same as `-O4`
- Color files with multiple hard links correctly
- Treat `-`, `)`, and `,` as paths when required to by POSIX
  - `)` and `,` are only supported before the expression begins
- Implement `-D opt`
- Implement `-D rates`
- Implement `-fprint`
- Implement `-fprint0`
- Implement BSD's `-f`
- Suggest fixes for typo'd arguments

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
