
# Makefile

<br>

## Parallel

You can run a **[Parallel Build]**  with:

```sh
make -j < Processor Count >
```
```sh
make -j 2
```

<br>

---

<br>


## Targets

```sh
make < Target >
```

<br>

   Target   | Description
:----------:|:-----------
 **/**      | Builds Binary
`all`       | Builds Everything
`check`     | Builds Everything + Runs Tests
`install`   | Installs:<br>- **BFS**<br>- **Man Page**<br>- **Shell Completions**
`uninstall` | Removes **BFS**

<br>

---

<br>

## Shorthands

```sh
make < Shorthand >
```

<br>

Shorthand | Description
:--------:|:-----------
`release` | Builds Binary <br>**+** *Optimizations*<br>**+** *LTO*<br>**-** *Assertions*
`gcov`    | Enables **[Code Coverage]**

<br>

Shorthand | Enables Sanitizer
:--------:|:-----------------:
`asan`    | **[Address]**
`lsan`    | **[Leak]**
`msan`    | **[Memory]**
`tsan`    | **[Thread]**
`ubsan`   | **[Undefined Behavior]**

<br>

*You can combine mutlitple flags &* <br>
*targets, but not all work together.*

```sh
make asan ubsan check
```

<br>

---

<br>

## Flags

*These flags are controlled with make / environment variables.*

<br>

 Flag | Description
:----:|:------------
`CC`                         | What compiler to use 🠖 `make CC=clang`
`CFLAGS`<br>`EXTRA_CFLAGS`   | Override / Add to the default compiler flags
`LDFLAGS`<br>`EXTRA_LDFLAGS` | Override / Add to the linker flags
`WITH_ACL`<br>`WITH_ATTR`    | Enable / Disable optional dependencies
`TEST_FLAGS`                 | `tests.sh` flags for `make check`
`DESTDIR`                    | The root directory for `make install`
`PREFIX`                     | The installation prefix ( default : `/usr` )
`MANDIR`                     | The man page installation directory

<br>

*Check the **[Makefile]** for more flags.*

<br>

---

<br>

## Dependencies

*The build system automatically tracks <br>
header dependencies with the family <br>
of `-M` compiler options.*

⤷ See `DEPFLAGS` in [`Makefile`][Makefile]

<br>

### Changes

This means that changes to a header file will <br>
automatically have object files using it be rebuilt.

<br>

### Beyond

We go one step further than most build <br>
systems by tracking the flags that were <br>
used for the previous compilation.

This enables you to change your build config <br>
without having to first call `make clean` .

```sh
make            # Builds In Debug Mode
make release    # Rebuilds In Release Mode
```

<br>

### Side Effects

A side effect that may surprise you is that to check <br>
a non - standard build you have to specify both the <br>
build target as well as `check` .

```sh
make < Target > check
```
```sh
make release check
```


<!----------------------------------------------------------------------------->

[Undefined Behavior]: https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
[Parallel Build]: https://www.gnu.org/software/make/manual/html_node/Parallel.html
[code coverage]: https://gcc.gnu.org/onlinedocs/gcc/Gcov.html
[Address]: https://github.com/google/sanitizers/wiki/AddressSanitizer
[Thread]: https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual
[Memory]: https://github.com/google/sanitizers/wiki/MemorySanitizer
[Leak]: https://github.com/google/sanitizers/wiki/AddressSanitizerLeakSanitizer#stand-alone-mode

[Makefile]: ../Makefile
