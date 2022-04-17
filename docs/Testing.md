
# Testing

<br>

## TestSuite

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


<!----------------------------------------------------------------------------->

[CI]: https://github.com/tavianator/bfs/actions

[Predefined Truths]: ../tests
[Tests]: ../tests.sh