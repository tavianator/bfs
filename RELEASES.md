1.*
===


1.6
---

**February 25, 2020**

- Implemented `-newerXt` (explicit reference times), `-since`, `-asince`, etc.
- Fixed `-empty` to skip special files (pipes, devices, sockets, etc.)


1.5.2
-----

**January 9, 2020**

- Fixed the build on NetBSD
- Added support for NFSv4 ACLs on FreeBSD
- Added a `+` after the file mode for files with ACLs in `-ls`
- Supported more file types (whiteouts, doors) in symbolic modes for `-ls`/`-printf %M`
- Implemented `-xattr` on FreeBSD


1.5.1
-----

**September 14, 2019**

- Added a warning to `-mount`, since it will change behaviour in the next POSIX revision
- Added a workaround for environments that block `statx()` with `seccomp()`, like older Docker
- Fixed coloring of nonexistent leading directories
- Avoided calling `stat()` on all mount points at startup


1.5
---

**June 27, 2019**

- New `-xattr` predicate to find files with extended attributes
- Fixed the `-acl` implementation on macOS
- Implemented depth-first (`-S dfs`) and iterative deepening search (`-S ids`)
- Piped `-help` output into `$PAGER` by default
- Fixed crashes on some invalid `LS_COLORS` values


1.4.1
-----

**April 5, 2019**

- Added a nicer error message when the tests are run as root
- Fixed detection of comparison expressions with signs, to match GNU find for things like `-uid ++10`
- Added support for https://no-color.org/
- Decreased the number of `stat()` calls necessary in some cases


1.4
---

**April 15, 2019**

- New `-unique` option that filters out duplicate files (https://github.com/tavianator/bfs/issues/40)
- Optimized the file coloring implementation
- Fixed the coloring implementation to match GNU ls more closely in many corner cases
  - Implemented escape sequence parsing for `LS_COLORS`
  - Implemented `ln=target` for coloring links like their targets
  - Fixed the order of fallbacks used when some color keys are unset
- Add a workaround for incorrect file types for bind-mounted files on Linux (https://github.com/tavianator/bfs/issues/37)


1.3.3
-----

**February 10, 2019**

- Fixed unpredictable behaviour for empty responses to `-ok`/`-okdir` caused by an uninitialized string
- Writing to standard output now causes `bfs` to fail if the descriptor was closed
- Fixed incomplete file coloring in error messages
- Added some data flow optimizations
- Fixed `-nogroup`/`-nouser` in big directory trees
- Added `-type w` for whiteouts, as supported by FreeBSD `find`
- Re-wrote the `-help` message and manual page


1.3.2
-----

**January 11, 2019**

- Fixed an out-of-bounds read if LS_COLORS doesn't end with a `:`
- Allowed multiple debug flags to be specified like `-D opt,tree`


1.3.1
-----

**January 3, 2019**

- Fixed some portability problems affecting FreeBSD


1.3
---

**January 2, 2019**

New features:

- `-acl` finds files with non-trivial Access Control Lists (from FreeBSD)
- `-capable` finds files with capabilities set
- `-D all` turns on all debugging flags at once

Fixes:

- `LS_COLORS` handling has been improved:
  - Extension colors are now case-insensitive like GNU `ls`
  - `or` (orphan) and `mi` (missing) files are now treated differently
  - Default colors can be unset with `di=00` or similar
  - Specific colors fall back to more general colors when unspecified in more places
  - `LS_COLORS` no longer needs a trailing colon
- `-ls`/`-fls` now prints the major/minor numbers for device nodes
- `-exec ;` is rejected rather than segfaulting
- `bfs` now builds on old Linux versions that require `-lrt` for POSIX timers
- For files whose access/change/modification times can't be read, `bfs` no longer fails unless those times are needed for tests
- The testsuite is now more correct and portable


1.2.4
-----

**September 24, 2018**

- GNU find compatibility fixes for `-printf`:
  - `%Y` now prints `?` if an error occurs resolving the link
  - `%B` is now supported for birth/creation time (as well as `%W`/`%w`)
  - All standard `strftime()` formats are supported, not just the ones from the GNU find manual
- Optimizations are now re-run if any expressions are reordered
- `-exec` and friends no longer leave zombie processes around when `exec()` fails


1.2.3
-----

**July 15, 2018**

- Fixed `test_depth_error` on filesystems that don't fill in `d_type`
- Fixed the build on Linux architectures that don't have the `statx()` syscall (ia64, sh4)
- Fixed use of AT_EMPTY_PATH for fstatat on systems that don't support it (Hurd)
- Fixed `ARG_MAX` accounting on architectures with large pages (ppc64le)
- Fixed the build against the upcoming glibc 2.28 release that includes its own `statx()` wrapper


1.2.2
-----

**June 23, 2018**

- Minor bug fixes:
  - Fixed `-exec ... '{}' +` argument size tracking after recovering from `E2BIG`
  - Fixed `-fstype` if `/proc` is available but `/etc/mtab` is not
  - Fixed an uninitialized variable when given `-perm +rw...`
  - Fixed some potential "error: 'path': Success" messages
- Reduced reliance on GNU coreutils in the testsuite
- Refactored and simplified the internals of `bftw()`


1.2.1
-----

**February 8, 2018**

- Performance optimizations


1.2
---

**January 20, 2018**

- Added support for the `-perm +7777` syntax deprecated by GNU find (equivalent to `-perm /7777`), for compatibility with BSD finds
- Added support for file birth/creation times on platforms that report it
  - `-Bmin`/`-Btime`/`-Bnewer`
  - `B` flag for `-newerXY`
  - `%w` and `%Wk` directives for `-printf`
  - Uses the `statx(2)` system call on new enough Linux kernels
- More robustness to `E2BIG` added to the `-exec` implementation


1.1.4
-----

**October 27, 2017**

- Added a man page
- Fixed cases where multiple actions write to the same file
- Report errors that occur when closing files/flushing streams
- Fixed "argument list too long" errors with `-exec ... '{}' +`


1.1.3
-----

**October 4, 2017**

- Refactored the optimizer
- Implemented data flow optimizations


1.1.2
-----

**September 10, 2017**

- Fixed `-samefile` and similar predicates when passed broken symbolic links
- Implemented `-fstype` on Solaris
- Fixed `-fstype` under musl
- Implemented `-D search`
- Implemented a cost-based optimizer


1.1.1
-----

**August 10, 2017**

- Re-licensed under the BSD Zero Clause License
- Fixed some corner cases with `-exec` and `-ok` parsing


1.1
---

**July 22, 2017**

- Implemented some primaries from NetBSD `find`:
  - `-exit [STATUS]` (like `-quit`, but with an optional explicit exit status)
  - `-printx` (escape special characters for `xargs`)
  - `-rm` (alias for `-delete`)
- Warn if `-prune` will have no effect due to `-depth`
- Handle y/n prompts according to the user's locale
- Prompt the user to correct typos without having to re-run `bfs`
- Fixed handling of paths longer than `PATH_MAX`
- Fixed spurious "Inappropriate ioctl for device" errors when redirecting `-exec ... +` output
- Fixed the handling of paths that treat a file as a directory (e.g. `a/b/c` where `a/b` is a regular file)
- Fixed an expression optimizer bug that broke command lines like `bfs -name '*' -o -print`


1.0.2
-----

**June 15, 2017**

Bugfix release.

- Fixed handling of \0 inside -printf format strings
- Fixed `-perm` interpretation of permcopy actions (e.g. `u=rw,g=r`)


1.0.1
-----

**May 17, 2017**

Bugfix release.

- Portability fixes that mostly affect GNU Hurd
- Implemented `-D exec`
- Made `-quit` not disable the implicit `-print`


1.0
---

**April 24, 2017**

This is the first release of bfs with support for all of GNU find's primitives.

Changes since 0.96:

- Implemented `-fstype`
- Implemented `-exec/-execdir ... +`
- Implemented BSD's `-X`
- Fixed the tests under Bash 3 (mostly for macOS)
- Some minor optimizations and fixes


0.*
===


0.96
----

**March 11, 2017**

73/76 GNU find features supported.

- Implemented -nouser and -nogroup
- Implemented -printf and -fprintf
- Implemented -ls and -fls
- Implemented -type with multiple types at once (e.g. -type f,d,l)
- Fixed 32-bit builds
- Fixed -lname on "symlinks" in Linux /proc
- Fixed -quit to take effect as soon as it's reached
- Stopped redirecting standard input from /dev/null for -ok and -okdir, as that violates POSIX
- Many test suite improvements


0.88
----

**December 20, 2016**

67/76 GNU find features supported.

- Fixed the build on macOS, and some other UNIXes
- Implemented `-regex`, `-iregex`, `-regextype`, and BSD's `-E`
- Implemented `-x` (same as `-mount`/`-xdev`) from BSD
- Implemented `-mnewer` (same as `-newer`) from BSD
- Implemented `-depth N` from BSD
- Implemented `-sparse` from FreeBSD
- Implemented the `T` and `P` suffices for `-size`, for BSD compatibility
- Added support for `-gid NAME` and `-uid NAME` as in BSD


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
