
# Hacking

**BFS** is written in **[C11]**.

*You can get a feel for the coding style* <br>
*by skimming through the **[Source Code]** .*

<br>

## Style

- <kbd>Tabs</kbd> for indentation

- <kbd>Spaces</kbd> for alignment

- Prefix types and functions with `bfs_`

  *Exceptions are things that could be* <br>
  *generally useful outside of **BFS** .*

- Error handling follows the C STL convention

  *Return a* `NULL` *pointer or a nonzero* <br>
  `int` *with the error code in* `errno` *.*
  
  ***All failure cases should be handled,*** <br>
  ***including***  `malloc()`  ***failures.***

- Use `goto` for cleanups in error paths.

<br>

## Coverage

*Both new **Features** and bug **Fixes** should have associated tests.*

<br>

### Adding

Create a test by adding a function in **[`tests.sh`][Tests]** .

Name it `test_< Label >` .

<br>

### Comparing

Snapshot tests can automatically compare the <br>
generated output to expected results by using <br>
the `bfs_diff` function.

```sh
function test_something(){
    bfs_diff basic -name something
}
```

<br>

### Directories

There are multiple directory trees generated for test cases:

- `rainbow`

- `basic`

- `links`

- `loops`

- `deep`

<br>

### Updating

To add a reference snapshot for your new test, run:

```sh
./tests.sh test_something --update
```

*Don't forget to add this change to your commit.*

<br>

### Compatibility

Add your test to one of the lists, <br>
depending on it's compatibility.

- `posix_tests`

- `bsd_tests`

- `gnu_tests`

- `bfs_tests`


<!----------------------------------------------------------------------------->

[C11]: https://en.wikipedia.org/wiki/C11_(C_standard_revision)

[Source Code]: ../src/main.c
[Tests]: ../tests.sh