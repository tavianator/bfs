3.*
===

3.1.3
-----

**March 6, 2024**

### Bug fixes

- On Linux, the `io_uring` feature probing introduced in `bfs` 3.1.2 only applied to one thread, causing all other threads to avoid using io_uring entirely.
  The probe results are now copied to all threads correctly.
  ([`f64f76b`](https://github.com/tavianator/bfs/commit/f64f76b55400b71e8576ed7e4a377eb5ef9576aa))


3.1.2
-----

**February 29, 2024**

### Bug fixes

- On Linux, we now check for supported `io_uring` operations before using them, which should fix `bfs` on 5.X series kernels that support `io_uring` but not all of `openat()`/`close()`/`statx()` ([`8bc72d6`](https://github.com/tavianator/bfs/commit/8bc72d6c20c5e38783c4956c4d9fde9b3ee9140c))

- Fixed a test failure triggered by certain filesystem types for `/tmp` ([#131](https://github.com/tavianator/bfs/issues/131))

- Fixed parsing and interpretation of timezone offsets for explicit reference times used in `-*since` and `-newerXt` ([`a9f3cde`](https://github.com/tavianator/bfs/commit/a9f3cde30426b546ba6e3172e1a7951213a72049))

- Fixed the build on m68k ([`c749c11`](https://github.com/tavianator/bfs/commit/c749c11b04444ca40941dd2ddc5802faed148f6a))


3.1.1
-----

**February 16, 2024**

### Changes

- Performance and scalability improvements

- The file count in `bfs -status` now has a thousands separator


3.1
---

**February 6, 2024**

### New features

- On Linux, `bfs` now uses [io_uring](https://en.wikipedia.org/wiki/Io_uring) for async I/O

- On all platforms, `bfs` can now perform `stat()` calls in parallel, accelerating queries like `-links`, `-newer`, and `-size`, as well as colorized output

- On FreeBSD, `-type w` now works to find whiteouts like the system `find`

### Changes

- Improved `bfs -j2` performance ([`b2ab7a1`](https://github.com/tavianator/bfs/commit/b2ab7a151fca517f4879e76e626ec85ad3de97c7))

- Optimized `-exec` by using `posix_spawn()` when possible, which can avoid the overhead of `fork()` ([`95fbde1`](https://github.com/tavianator/bfs/commit/95fbde17a66377b6fbe7ff1f014301dbbf09270d))

- `-execdir` and `-okdir` are now rejected if `$PATH` contains a relative path, matching the behaviour of GNU find ([`163baf1`](https://github.com/tavianator/bfs/commit/163baf1c9af13be0ce705b133e41e0c3d6427398))

- Leading whitespace is no longer accepted in integer command line arguments like `-links ' 1'` ([`e0d7dc5`](https://github.com/tavianator/bfs/commit/e0d7dc5dfd7bdaa62b6bc18e9c1cce00bbe08577))

### Bug fixes

- `-quit` and `-exit` could be ignored in the iterative deepening modes (`-S {ids,eds}`).
  This is now fixed ([`670ebd9`](https://github.com/tavianator/bfs/commit/670ebd97fb431e830b1500b2e7e8013b121fb2c5)).
  The bug was introduced in version 3.0.3 (commit [`5f16169`]).

- Fixed two possible errors in sort mode (`-s`):
  - Too many open files ([`710c083`](https://github.com/tavianator/bfs/commit/710c083ff02eb1cc5b8daa6778784f3d1cd3c08d))
  - Out of memory ([`76ffc8d`](https://github.com/tavianator/bfs/commit/76ffc8d30cb1160d55d855d8ac630a2b9075fbcf))

- Fixed handling of FreeBSD union mounts ([`3ac3bee`](https://github.com/tavianator/bfs/commit/3ac3bee7b0d9c9be693415206efa664bf4a7d4a7))

- Fixed `NO_COLOR` handling when it's set to the empty string ([`79aee58`](https://github.com/tavianator/bfs/commit/79aee58a4621d01c4b1e98c332775f3b87213ddb))

- Fixed some portability issues:
  - [OpenBSD](https://github.com/tavianator/bfs/compare/ee200c07643801c8b53e5b80df704ecbf77a884e...79f1521b0e628be72bed3a648f0ae90b62fc69b8)
  - [NetBSD](https://github.com/tavianator/bfs/compare/683f2c41c72efcb82ce866e3dcc311ac9bd8b66d...6435684a7d515e18247ae1b3dd9ec8681fee22d0)
  - [DragonFly BSD](https://github.com/tavianator/bfs/compare/08867473e75e8e20ca76c7fb181204839e28b271...45fb1d952c3b262278a3b22e9c7d60cca19a5407)
  - [Illumos](https://github.com/tavianator/bfs/compare/4010140cb748cc4f7f57b0a3d514485796c665ce...ae94cdc00136685abe61d55e1e357caaa636d785)


3.0.4
-----

**October 12, 2023**

### Bug fixes

- Fixed a segfault when reporting errors under musl ([`d40eb87`])

[`d40eb87`]: https://github.com/tavianator/bfs/commit/d40eb87cc00f50a5debb8899eacb7fcf1065badf


3.0.3
-----

**October 12, 2023**

### Changes

- Iterative deepening modes (`-S {ids,eds}`) were optimized by delaying teardown until the very end ([`5f16169`])

- Parallel depth-first search (`-S dfs`) was optimized to avoid enqueueing every file separately ([`2572273`])

### Bug fixes

- Iterative deepening modes (`-S {ids,eds}`) were performing iterative *breadth*-first searches since `bfs` 3.0, negating any advantages they may have had over normal breadth-first search.
  They now do iterative *depth*-first searches as expected.
  ([`a029d95`])

- Fixed a linked-list corruption that could lead to an infinite loop on macOS and other non-Linux, non-FreeBSD platforms ([`773f4a4`])

[`5f16169`]: https://github.com/tavianator/bfs/commit/5f1616912ba3a7a23ce6bce02df3791b73da38ab
[`2572273`]: https://github.com/tavianator/bfs/commit/257227326fe60fe70e80433fd34d1ebcb2f9f623
[`a029d95`]: https://github.com/tavianator/bfs/commit/a029d95b5736a74879f32089514a5a6b63d6efbc
[`773f4a4`]: https://github.com/tavianator/bfs/commit/773f4a446f03da62d88e6d17be49fdc0a3e38465


3.0.2
-----

**September 6, 2023**

### Changes

- `-files0-from` now allows an empty set of paths to be given, matching GNU findutils 4.9.0

- Reduced memory consumption in multi-threaded searches

- Many man page updates

### Bug fixes

- Fixed an out-of-bounds memory read that could occur when escaping a string containing an incomplete multi-byte character


3.0.1
-----

**July 18, 2023**

### Bug fixes

- Traversal fixes that mostly affect large directory trees ([#107])

  - `bfs` could encounter `EMFILE`, close a file, and retry many times, particularly with `-j1`

  - Breadth-first search could become highly unbalanced, negating many of the benefits of `bfs`

  - On non-{Linux,FreeBSD} plaforms, directories could stay open longer than necessary, consuming extra memory

[#107]: https://github.com/tavianator/bfs/pull/107


3.0
---

**July 13, 2023**

### New features

- `bfs` now reads directories asynchronously and in parallel ([#101]).
  Performance is significantly improved as a result.
  Parallelism is controlled by the new `-j` flag, e.g. `-j1`, `-j2`, etc.

[#101]: https://github.com/tavianator/bfs/issues/101

### Changes

- `bfs` now uses the [C17] standard version, up from C11

- Due to [#101], `bfs` now requires some additional C and POSIX features:
  - [Standard C atomics] (`<stdatomic.h>`)
  - [POSIX threads] (`<pthread.h>`)

- `$LS_COLORS` extensions written in different cases (e.g. `*.jpg=35:*.JPG=01;35`) are now matched case-sensitively, to match the new behaviour of GNU ls since coreutils version 9.2

- Added a warning/error if `$LS_COLORS` can't be parsed, depending on whether `-color` is requested explicitly

- Filenames with control characters are now escaped when printing with `-color`

- Build flags like `WITH_ONIGURUMA` have been renamed to `USE_ONIGURUMA`

[C17]: https://en.cppreference.com/w/c/17
[Standard C atomics]: https://en.cppreference.com/w/c/atomic
[POSIX threads]: https://pubs.opengroup.org/onlinepubs/9699919799/idx/threads.html

### Bug fixes

- Fixed handling of the "normal text" color (`no` in `$LS_COLORS`) to match GNU ls


2.*
===

2.6.3
-----

**January 31, 2023**

- Fixed running the tests as root on Linux [`8b24de3`]

- Fixed some tests on Android [`2724dfb`] [`0a5a80c`]

- Stopped relying on non-POSIX touch(1) features in the tests.
  This should fix the tests on at least OpenBSD.
  [`2d5edb3`]

- User/group caches are now filled lazily instead of eagerly [`b41dca5`]

- More caches and I/O streams are flushed before -exec/-ok [`f98a1c4`]

- Fixed various memory safety issues found by fuzzing \
  [`712b137`] [`5ce883d`] [`da02def`] [`c55e855`]

- Fixed a test failure on certain macOS versions [`8b24de3`]

- Mitigated a race condition when determining filesystem types ([#97])

- Lots of refactoring and optimization

[`8b24de3`]: https://github.com/tavianator/bfs/commit/8b24de3882ff5a3e33b82ab20bb4eadf134cf559
[`2724dfb`]: https://github.com/tavianator/bfs/commit/2724dfbd17552f892a0d8b39b96cbe9e49d66fdb
[`0a5a80c`]: https://github.com/tavianator/bfs/commit/0a5a80c98cc7e5d8735b615fa197a6cff2bb08cc
[`2d5edb3`]: https://github.com/tavianator/bfs/commit/2d5edb37b924715b4fbee4d917ac334c773fca61
[`b41dca5`]: https://github.com/tavianator/bfs/commit/b41dca52762c5188638236ae81b9f4597bb29ac9
[`f98a1c4`]: https://github.com/tavianator/bfs/commit/f98a1c4a1cf61ff7d6483388ca1fac365fb0b31b
[`712b137`]: https://github.com/tavianator/bfs/commit/712b13756a09014ef730c8f9b96da4dc2f09b762
[`5ce883d`]: https://github.com/tavianator/bfs/commit/5ce883daaafc69f83b01dac5db0647e9662a6e87
[`da02def`]: https://github.com/tavianator/bfs/commit/da02defb91c3a1bda0ea7e653d81f997f1c8884a
[`c55e855`]: https://github.com/tavianator/bfs/commit/c55e85580df10c5afdc6fc0710e756a456aa8e93
[`8b24de3`]: https://github.com/tavianator/bfs/commit/8b24de3882ff5a3e33b82ab20bb4eadf134cf559
[#97]: https://github.com/tavianator/bfs/issues/97


2.6.2
-----

**October 21, 2022**

- Fixed use of uninitialized memory on parsing errors involving `-fprintf`

- Fixed Android build issues ([#96])

- Refactored the test suite

[#96]: https://github.com/tavianator/bfs/issues/96


2.6.1
-----

**July 7, 2022**

- Fix `stat()` errors on GNU Hurd systems with glibc older than 2.35

- Added fish shell tab completion ([#94]).
  Thanks @xfgusta!

[#94]: https://github.com/tavianator/bfs/pull/94


2.6
---

**May 21, 2022**

- Fixed deleting large NFS directories on FreeBSD ([#67]).

- Added support for a `bfs`-specific `BFS_COLORS` environment variable.

- Refactored the build system, directory structure, and documentation ([#88], [#89], [#91]).
  Thanks @ElectronicsArchiver!

- Added `zsh` completion ([#86]).
  Thanks @VorpalBlade!

- Updated the default color scheme to match GNU coreutils 9.1.
  Files with capabilities set are no longer colored differently by default, resulting in a significant performance improvement.

- Became less aggressive at triggering automounts

- Added support for out-of-tree builds with `BUILDDIR`

[#67]: https://github.com/tavianator/bfs/issues/67
[#86]: https://github.com/tavianator/bfs/issues/86
[#88]: https://github.com/tavianator/bfs/issues/88
[#89]: https://github.com/tavianator/bfs/issues/89
[#91]: https://github.com/tavianator/bfs/issues/91


2.5
---

**March 27, 2022**

- Added compiler-style context for errors and warnings.
  Errors look like this:

      $ bfs -nam needle
      bfs: error: bfs -nam needle
      bfs: error:     ~~~~
      bfs: error: Unknown argument; did you mean -name?

  and warnings look like this:

      $ bfs -print -name 'needle'
      bfs: warning: bfs -print -name needle
      bfs: warning:            ~~~~~~~~~~~~
      bfs: warning: The result of this expression is ignored.

- Updated from C99 to C11

- Fixed the tests when built against musl

- Fixed a build error reported on Manjaro


2.4.1
-----

**February 24, 2022**

- Fixed the build when Oniguruma is not installed in the default search paths ([#82])

- Fixed string encoding bugs with Oniguruma enabled

- Fixed regex error reporting bugs

[#82]: https://github.com/tavianator/bfs/issues/82


2.4
---

**February 22, 2022**

- Added the Oniguruma regular expression library as an (optional, but enabled by default) dependency ([#81]).
  Oniguruma supports more regular expression syntax types than the POSIX regex API, and often performs better.
  To build `bfs` without this new dependency, do `make WITH_ONIGURUMA=` to disable it.
  Thanks @data-man!

- Added support for the `ed`, `emacs`, `grep`, and `sed` regular expression types ([#21])

- Before executing a process with `-exec[dir]`/`-ok[dir]`, `bfs` now ensures all output streams are flushed.
  Previously, I/O from subprocesses could be interleaved unpredictably with buffered I/O from `bfs` itself.

[#81]: https://github.com/tavianator/bfs/pull/81
[#21]: https://github.com/tavianator/bfs/issues/21


2.3.1
-----

**January 21, 2022**

- Fixed the build on Debian kFreeBSD

- Fixed a crash on GNU Hurd when piping bfs's output

- Fixed a double-`close()` on non-Linux platforms if `fdopendir()` fails

- Reduced memory allocations on startup


2.3
---

**November 25, 2021**

- More tweaks to `PAGER` and `LESS` handling for `bfs -help` ([#76])

- Use 512-byte blocks for `-ls` when `POSIXLY_CORRECT` is set ([#77])

- Implemented `-files0-from FILE` to take a list of `'\0'`-separated starting paths.
  GNU find will implement the same feature in an upcoming release.

- Added colors to `-printf` output ([#62])

- Faster recovery from `E2BIG` during `-exec`

[#76]: https://github.com/tavianator/bfs/issues/76
[#77]: https://github.com/tavianator/bfs/issues/77
[#62]: https://github.com/tavianator/bfs/issues/62


2.2.1
-----

**June 2, 2021**

- Fixed some incorrect coloring of broken links when links are being followed (`-L`)

- Made the tests work when run as root by dropping privileges.
  This may be helpful for certain packaging or CI environments, but is not recommended.

- Treat empty `PAGER` and `LESS` environment variables like they're unset, for `bfs -help` ([#71]).
  Thanks @markus-oberhumer!

- The soft `RLIMIT_NOFILE` is now raised automatically to a fairly large value when possible.
  This provides a minor performance benefit for large directory trees.

- Implemented time units for `-mtime` as found in FreeBSD find ([#75])

[#71]: https://github.com/tavianator/bfs/issues/71
[#75]: https://github.com/tavianator/bfs/issues/75


2.2
---

**March 6, 2021**

- Fixed `-hidden` on hidden start paths

- Added a Bash completion script.
  Thanks @bmundt6!

- Fixed rounding in `-used`.
  Corresponding fixes were made to GNU find in version 4.8.0.

- Optimized the open directory representation.
  On Linux, much libc overhead is bypassed by issuing syscalls directly.
  On all platforms, a few fewer syscalls and open file descriptors will be used.

- Implemented `-flags` from BSD find


2.1
---

**November 11, 2020**

- Added a new `-status` option that displays the search progress in a bar at the bottom of the terminal

- Fixed an optimizer bug introduced in version 2.0 that affected some combinations of `-user`/`-group` and `-nouser`/`-nogroup`


2.0
---

**October 14, 2020**

- [#8]: New `-exclude <expression>` syntax to more easily and reliably filter out paths.
  For example:

      bfs -name config -exclude -name .git

  will find all files named `config`, without searching any directories (or files) named `.git`.
  In this case, the same effect could have been achieved (more awkwardly) with `-prune`:

      bfs ! \( -name .git -prune \) -name config

  But `-exclude` will work in more cases:

      # -exclude works with -depth, while -prune doesn't:
      bfs -depth -name config -exclude -name .git

      # -exclude applies even to paths below the minimum depth:
      bfs -mindepth 3 -name config -exclude -name .git

- [#30]: `-nohidden` is now equivalent to `-exclude -hidden`.
  This changes the behavior of command lines like

      bfs -type f -nohidden

  to do what was intended.

- Optimized the iterative deepening (`-S ids`) implementation

- Added a new search strategy: exponential deepening search (`-S eds`).
  This strategy provides many of the benefits of iterative deepening, but much faster due to fewer re-traversals.

- Fixed an optimizer bug that could skip `-empty`/`-xtype` if they didn't always lead to an action

- Implemented `-xattrname` to find files with a particular extended attribute (from macOS find)

- Made `-printf %l` still respect the width specifier (e.g. `%10l`) for non-links, to match GNU find

- Made `bfs` fail if `-color` is given explicitly and `LS_COLORS` can't be parsed, rather than falling back to non-colored output

[#8]: https://github.com/tavianator/bfs/issues/8
[#30]: https://github.com/tavianator/bfs/issues/30


1.*
===

1.7
---

**April 22, 2020**

- Fixed `-ls` printing numeric IDs instead of user/group names in large directory trees
- Cached the user and group tables for a performance boost
- Fixed interpretation of "default" ACLs
- Implemented `-s` flag to sort results


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

- New `-unique` option that filters out duplicate files ([#48])
- Optimized the file coloring implementation
- Fixed the coloring implementation to match GNU ls more closely in many corner cases
  - Implemented escape sequence parsing for `LS_COLORS`
  - Implemented `ln=target` for coloring links like their targets
  - Fixed the order of fallbacks used when some color keys are unset
- Add a workaround for incorrect file types for bind-mounted files on Linux ([#37])

[#48]: https://github.com/tavianator/bfs/issues/48
[#37]: https://github.com/tavianator/bfs/issues/37


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

- Fixed [#7] again
- Like GNU find, don't print warnings by default if standard input is not a terminal
- Redirect standard input from /dev/null for -ok and -okdir
- Skip . when -delete'ing
- Fixed -execdir when the root path has no slashes
- Fixed -execdir in /
- Support -perm +MODE for symbolic modes
- Fixed the build on FreeBSD

[#7]: https://github.com/tavianator/bfs/issues/7


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
- Fix [#7]
- Fix `-daystart`'s rounding of midnight
- Implement (most of) `-newerXY`
- Implement `-used`
- Implement `-size`

[#7]: https://github.com/tavianator/bfs/issues/7


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
