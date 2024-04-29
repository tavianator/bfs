Building `bfs`
==============

Compiling
---------

A simple invocation of

    $ ./configure
    $ make

should build `bfs` successfully.
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
| `make clean`     | Delete the build products                                     |
| `make distclean` | Delete all generated files, including the build configuration |

### Build profiles

The configuration system provides a few shorthand flags for handy configurations:

| Command                 | Description                                                 |
|-------------------------|-------------------------------------------------------------|
| `./configure RELEASE=y` | Build `bfs` with optimizations, LTO, and without assertions |
| `./configure ASAN=y`    | Enable [AddressSanitizer]                                   |
| `./configure LSAN=y`    | Enable [LeakSanitizer]                                      |
| `./configure MSAN=y`    | Enable [MemorySanitizer]                                    |
| `./configure TSAN=y`    | Enable [ThreadSanitizer]                                    |
| `./configure UBSAN=y`   | Enable [UndefinedBehaviorSanitizer]                         |
| `./configure GCOV=y`    | Enable [code coverage]                                      |

[AddressSanitizer]: https://github.com/google/sanitizers/wiki/AddressSanitizer
[LeakSanitizer]: https://github.com/google/sanitizers/wiki/AddressSanitizerLeakSanitizer#stand-alone-mode
[MemorySanitizer]: https://github.com/google/sanitizers/wiki/MemorySanitizer
[ThreadSanitizer]: https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual
[UndefinedBehaviorSanitizer]: https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
[code coverage]: https://gcc.gnu.org/onlinedocs/gcc/Gcov.html

You can combine multiple profiles (e.g. `./configure ASAN=y UBSAN=y`), but not all of them will work together.

### Flags

Other flags can be specified on the `./configure` command line or in the environment.
Here are some of the common ones; check the [`Makefile`](/Makefile) for more.

| Flag                                | Description                                        |
|-------------------------------------|----------------------------------------------------|
| `CC`                                | The C compiler to use, e.g. `./configure CC=clang` |
| `CFLAGS`<br>`EXTRA_CFLAGS`          | Override/add to the default compiler flags         |
| `LDFLAGS`<br>`EXTRA_LDFLAGS`        | Override/add to the linker flags                   |
| `USE_LIBACL`<br>`USE_LIBCAP`<br>... | Enable/disable [optional dependencies]             |
| `TEST_FLAGS`                        | `tests.sh` flags for `make check`                  |
| `DESTDIR`                           | The root directory for `make install`              |
| `PREFIX`                            | The installation prefix (default: `/usr`)          |
| `MANDIR`                            | The man page installation directory                |

[optional dependencies]: #dependencies

### Dependencies

`bfs` depends on some system libraries for some of its features.
These dependencies are optional, and can be turned off in `./configure` if necessary by setting the appropriate variable to `n` (e.g. `./configure USE_ONIGURUMA=n`).

| Dependency   | Platforms  | `./configure` flag |
|--------------|------------|--------------------|
| [libacl]     | Linux only | `USE_LIBACL`       |
| [libcap]     | Linux only | `USE_LIBCAP`       |
| [liburing]   | Linux only | `USE_LIBURING`     |
| [libselinux] | Linux only | `USE_LIBSELINUX`   |
| [Oniguruma]  | All        | `USE_ONIGURUMA`    |

[libacl]: https://savannah.nongnu.org/projects/acl
[libcap]: https://sites.google.com/site/fullycapable/
[libselinux]: https://github.com/SELinuxProject/selinux
[liburing]: https://github.com/axboe/liburing
[Oniguruma]: https://github.com/kkos/oniguruma

### Dependency tracking

The build system automatically tracks header dependencies with the `-M` family of compiler options (see `DEPFLAGS` in [`build/deps.mk`](/build/deps.mk)).
So if you edit a header file, `make` will rebuild the necessary object files ensuring they don't go out of sync.

We also add a dependency on the current configuration, so you can change configurations and rebuild without having to `make clean`.
For example,

    $ ./configure
    $ make
    $ ./configure RELEASE=y
    $ make

will build the project in debug mode and then rebuild it in release mode.

### Out-of-tree builds

You can set up an out-of-tree build by running the `configure` script from another directory, for example:

    $ mkdir out
    $ cd out
    $ ../configure
    $ make


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
