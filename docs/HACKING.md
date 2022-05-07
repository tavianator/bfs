Hacking on `bfs`
================

License
-------

`bfs` is licensed under the [Zero-Clause BSD License](https://opensource.org/licenses/0BSD), a maximally permissive license.
Contributions must use the same license.


Implementation
--------------

`bfs` is written in [C](https://en.wikipedia.org/wiki/C_(programming_language)), specifically [C11](https://en.wikipedia.org/wiki/C11_(C_standard_revision)).
You can get a feel for the coding style by skimming the source code.
[`main.c`](/src/main.c) contains an overview of the rest of source files.
A quick summary:

- Tabs for indentation, spaces for alignment.
- Most types and functions should be namespaced with `bfs_`.
  Exceptions are made for things that could be generally useful outside of `bfs`.
- Error handling follows the C standard library conventions: return a nonzero `int` or a `NULL` pointer, with the error code in `errno`.
  All failure cases should be handled, including `malloc()` failures.
- `goto` is not considered harmful for cleaning up in error paths.


Tests
-----

`bfs` includes an extensive test suite.
See the [build documentation](BUILDING.md#testing) for details on running the tests.

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
Finally, add the test case to one of the arrays `posix_tests`, `bsd_tests`, `gnu_tests`, or `bfs_tests`, depending on which `find` implementations it should be compatible with.
