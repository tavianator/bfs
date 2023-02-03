Hacking on `bfs`
================

License
-------

`bfs` is licensed under the [Zero-Clause BSD License](https://opensource.org/licenses/0BSD), a maximally permissive license.
Contributions must use the same license.

Individual files contain the following tag instead of the full license text:

    SPDX-License-Identifier: 0BSD

This enables machine processing of license information based on the SPDX License Identifiers that are available here: https://spdx.org/licenses/


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

Test cases are grouped by the standard or `find` implementation that supports the tested feature(s):

| Group                           | Description                           |
|---------------------------------|---------------------------------------|
| [`tests/posix`](/tests/posix)   | POSIX compatibility tests             |
| [`tests/bsd`](/tests/bsd)       | BSD `find` features                   |
| [`tests/gnu`](/tests/gnu)       | GNU `find` features                   |
| [`tests/common`](/tests/common) | Features common to BSD and GNU `find` |
| [`tests/bfs`](/tests/bfs)       | `bfs`-specific tests                  |

Both new features and bug fixes should have associated tests.
To add a test, create a new `*.sh` file in the appropriate group.
Snapshot tests use the `bfs_diff` function to automatically compare the generated and expected outputs.
For example,

```bash
# posix/something.sh
bfs_diff basic -name something
```

`basic` is one of the directory trees generated for test cases; others include `links`, `loops`, `deep`, and `rainbow`.

Run `./tests/tests.sh posix/something --update` to generate the reference snapshot (and don't forget to `git add` it).
