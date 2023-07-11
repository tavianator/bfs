Building `bfs`
==============

Compiling
---------

`bfs` uses [GNU Make](https://www.gnu.org/software/make/) as its build system.
A simple invocation of

    $ make

should build `bfs` successfully, with no additional steps necessary.
As usual with `make`, you can run a [parallel build](https://www.gnu.org/software/make/manual/html_node/Parallel.html) with `-j`.
For example, to use all your cores, run `make -j$(nproc)`.

### Targets

| Command          | Description                                                   |
|------------------|---------------------------------------------------------------|
| `make`           | Builds just the `bfs` binary                                  |
| `make all`       | Builds everything, including the tests (but doesn't run them) |
| `make check`     | Builds everything, and runs the tests                         |
| `make install`   | Installs `bfs` (with man page, shell completions, etc.)       |
| `make uninstall` | Uninstalls `bfs`                                              |

### Flag-like targets

The build system provides a few shorthand targets for handy configurations:

| Command        | Description                                                 |
|----------------|-------------------------------------------------------------|
| `make release` | Build `bfs` with optimizations, LTO, and without assertions |
| `make asan`    | Enable [AddressSanitizer]                                   |
| `make lsan`    | Enable [LeakSanitizer]                                      |
| `make msan`    | Enable [MemorySanitizer]                                    |
| `make tsan`    | Enable [ThreadSanitizer]                                    |
| `make ubsan`   | Enable [UndefinedBehaviorSanitizer]                         |
| `make gcov`    | Enable [code coverage]                                      |

[AddressSanitizer]: https://github.com/google/sanitizers/wiki/AddressSanitizer
[LeakSanitizer]: https://github.com/google/sanitizers/wiki/AddressSanitizerLeakSanitizer#stand-alone-mode
[MemorySanitizer]: https://github.com/google/sanitizers/wiki/MemorySanitizer
[ThreadSanitizer]: https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual
[UndefinedBehaviorSanitizer]: https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
[code coverage]: https://gcc.gnu.org/onlinedocs/gcc/Gcov.html

You can combine multiple flags and other targets (e.g. `make asan ubsan check`), but not all of them will work together.

### Flags

Other flags are controlled with `make` variables and/or environment variables.
Here are some of the common ones; check the [`GNUmakefile`](/GNUmakefile) for more.

| Flag                             | Description                                 |
|----------------------------------|---------------------------------------------|
| `CC`                             | The C compiler to use, e.g. `make CC=clang` |
| `CFLAGS`<br>`EXTRA_CFLAGS`       | Override/add to the default compiler flags  |
| `LDFLAGS`<br>`EXTRA_LDFLAGS`     | Override/add to the linker flags            |
| `USE_ACL`<br>`USE_ATTR`<br>...   | Enable/disable [optional dependencies]      |
| `TEST_FLAGS`                     | `tests.sh` flags for `make check`           |
| `BUILDDIR`                       | The build output directory (default: `.`)   |
| `DESTDIR`                        | The root directory for `make install`       |
| `PREFIX`                         | The installation prefix (default: `/usr`)   |
| `MANDIR`                         | The man page installation directory         |

[optional dependencies]: #dependencies

### Dependencies

`bfs` depends on some system libraries for some of its features.
These dependencies are optional, and can be turned off at build time if necessary by setting the appropriate variable to the empty string (e.g. `make USE_ONIGURUMA=`).

| Dependency  | Platforms  | `make` flag     |
|-------------|------------|-----------------|
| [acl]       | Linux only | `USE_ACL`       |
| [attr]      | Linux only | `USE_ATTR`      |
| [libcap]    | Linux only | `USE_LIBCAP`    |
| [liburing]  | Linux only | `USE_LIBURING`  |
| [Oniguruma] | All        | `USE_ONIGURUMA` |

[acl]: https://savannah.nongnu.org/projects/acl
[attr]: https://savannah.nongnu.org/projects/attr
[libcap]: https://sites.google.com/site/fullycapable/
[liburing]: https://github.com/axboe/liburing
[Oniguruma]: https://github.com/kkos/oniguruma

### Dependency tracking

The build system automatically tracks header dependencies with the `-M` family of compiler options (see `DEPFLAGS` in the [`GNUmakefile`](/GNUmakefile)).
So if you edit a header file, `make` will rebuild the necessary object files ensuring they don't go out of sync.

We go one step further than most build systems by tracking the flags that were used for the previous compilation.
That means you can change configurations without having to `make clean`.
For example,

    $ make
    $ make release

will build the project in debug mode and then rebuild it in release mode.

A side effect of this may be surprising: `make check` by itself will rebuild the project in the default configuration.
To test a different configuration, you'll have to repeat it (e.g. `make release check`).


Testing
-------

`bfs` comes with an extensive test suite which can be run with

    $ make check

The test harness is implemented in the file [`tests/tests.sh`](/tests/tests.sh).
Individual test cases are found in `tests/*/*.sh`.
Most of them are *snapshot tests* which compare `bfs`'s output to a known-good copy saved under the matching `tests/*/*.out`.

You can pass the name of a particular test case (or a few) to run just those tests.
For example:

    $ ./tests/tests.sh posix/basic

If you need to update the reference snapshot, pass `--update`.
It can be handy to generate the snapshot with a different `find` implementation to ensure the output is correct, for example:

    $ ./tests/tests.sh posix/basic --bfs=find --update

But keep in mind, other `find` implementations may not be correct.
To my knowledge, no other implementation passes even the POSIX-compatible subset of the tests:

    $ ./tests/tests.sh --bfs=find --posix
    ...
    tests passed: 90
    tests skipped: 3
    tests failed: 6

Run

    $ ./tests/tests.sh --help

for more details.

### Validation

A more thorough testsuite is run by the [CI](https://github.com/tavianator/bfs/actions) and to validate releases.
It builds `bfs` in multiple configurations to test for latent bugs, memory leaks, 32-bit compatibility, etc.
You can run it yourself with

    $ make distcheck

Some of these tests require `sudo`, and will prompt for your password if necessary.
