
# Contribute

<br>

## License

**BFS** and any contributions to it are licensed under <br>**[Zero - Clause BSD]**, a maximally permissive license.


Building
--------

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
Here are some of the common ones; check the [`Makefile`](/Makefile) for more.

| Flag                             | Description                                 |
|----------------------------------|---------------------------------------------|
| `CC`                             | The C compiler to use, e.g. `make CC=clang` |
| `CFLAGS`<br>`EXTRA_CFLAGS`       | Override/add to the default compiler flags  |
| `LDFLAGS`<br>`EXTRA_LDFLAGS`     | Override/add to the linker flags            |
| `WITH_ACL`<br>`WITH_ATTR`<br>... | Enable/disable optional dependencies        |
| `TEST_FLAGS`                     | `tests.sh` flags for `make check`           |
| `DESTDIR`                        | The root directory for `make install`       |
| `PREFIX`                         | The installation prefix (default: `/usr`)   |
| `MANDIR`                         | The man page installation directory         |

### Dependency tracking

The build system automatically tracks header dependencies with the `-M` family of compiler options (see `DEPFLAGS` in the `Makefile`).
So if you edit a header file, `make` will rebuild the necessary object files ensuring they don't go out of sync.

We go one step further than most build systems by tracking the flags that were used for the previous compilation.
That means you can change configurations without having to `make clean`.
For example,

    $ make
    $ make release

will build the project in debug mode and then rebuild it in release mode.

A side effect of this may be surprising: `make check` by itself will rebuild the project in the default configuration.
To test a different configuration, you'll have to repeat it (e.g. `make release check`).


<br>

---

<br>

## Testing

**BFS** testsuite contains hundreds of separate tests, most of <br>
which are snapshot - tests and implemented in [`tests.sh`][Tests] .

*Snapshot-tests compare generated output to **[Predefined Truths]**.*

<br>

### Help

```sh
./tests.sh --help
```

<br>

### Run

##### All

```sh
make check
```

##### Specific

```sh
./tests.sh test_basic
```
```sh
./tests.sh test_basic test_bang
```

<br>

### Update

To update the reference snapshot, pass `--update` .

```sh
./tests.sh test_basic --bfs=find --update
```

*It can be handy to generate the snapshot with a different* <br>
`find` *implementation to ensure that the output is correct.*

<br>

### Implementations

***Other*** `find` ***implementations may not be correct.***

*To my knowledge, no other implementation even <br>
passes the POSIX - compatible subset of the tests.*

```console
foo@bar:~$ ./tests.sh --bfs=find --posix
...
tests passed: 89
tests failed: 5
```

<br>

---

<br>

## Validation

A more thorough testsuite is run by the **[CI]** .

<br>

*This builds **BFS** in multiple configurations to test for :*

- **32-bit Compatibility**

- **Memory Leaks**

- **Latent Bugs**

<br>

### Manual

You can run it yourself with:

```sh
make distcheck
```

<br>

*Some of theses tests require `sudo`* <br>
*privileges and will prompt you for it.*


<br>

---

<br>

Hacking
-------

`bfs` is written in [C](https://en.wikipedia.org/wiki/C_(programming_language)), specifically [C11](https://en.wikipedia.org/wiki/C11_(C_standard_revision)).
You can get a feel for the coding style by skimming the source code.
[`main.c`](src/main.c) contains an overview of the rest of source files.
A quick summary:

- Tabs for indentation, spaces for alignment.
- Most types and functions should be namespaced with `bfs_`.
  Exceptions are made for things that could be generally useful outside of `bfs`.
- Error handling follows the C standard library conventions: return a nonzero `int` or a `NULL` pointer, with the error code in `errno`.
  All failure cases should be handled, including `malloc()` failures.
- `goto` is not harmful for cleaning up in error paths.

### Adding tests

Both new features and bug fixes should have associated tests.
To add a test, create a new function in `tests.sh` called `test_<something>`.
Snapshot tests use the `bfs_diff` function to automatically compare the generated and expected outputs.
For example,

```bash
function test_something() {
    bfs_diff basic -name something
}
```

`basic` is one of the directory trees generated for test cases; others include `links`, `loops`, `deep`, and `rainbow`.

Run `./tests.sh test_something --update` to generate the reference snapshot (and don't forget to `git add` it).
Finally, add the test case to one of the arrays `posix_tests`, `bsd_tests`, `gnu_tests`, or `bfs_tests` depending on which `find` implementations it should be compatible with.

<!----------------------------------------------------------------------------->

[Zero - Clause BSD]: https://opensource.org/licenses/0BSD

[CI]: https://github.com/tavianator/bfs/actions

[Predefined Truths]: ../tests
[Tests]: ../tests.sh
