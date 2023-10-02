This directory contains a suite of benchmarks used to evaluate `bfs` and detect performance regressions.
To run them, you'll need the [tailfin] benchmark harness.
You can read the full usage information with

[tailfin]: https://github.com/tavianator/tailfin

```console
$ tailfin -n run bench/bench.sh --help
Usage: tailfin run bench/bench.sh [--default]
           [--complete] [--early-quit] [--print] [--strategies]
           [--build=...] [--bfs] [--find] [--fd]
           [--no-clean] [--help]
...
```

The benchmarks use various git repositories to have a realistic and reproducible directory structure as a corpus.
Currently, those are the [Linux], [Rust], and [Chromium] repos.
The scripts will automatically clone those repos using [partial clone] filters to avoid downloading the actual file contents, saving bandwidth and space.

[Linux]: https://github.com/torvalds/linux.git
[Rust]: https://github.com/rust-lang/rust.git
[Chromium]: https://chromium.googlesource.com/chromium/src.git
[partial clone]: https://git-scm.com/docs/partial-clone

You can try out a quick benchmark by running

```console
$ tailfin run bench/bench.sh --build=main --complete=linux
```

This will build the `main` branch, and measure the complete traversal of the Linux repo.
Results will be both printed to the console and saved in a Markdown file, which you can find by running

```console
$ tailfin latest
results/2023/09/29/15:32:49
$ cat results/2023/09/29/15:32:49/runs/1/bench.md
## Complete traversal
...
```

To measure performance improvements/regressions of a change, compare the `main` branch to the topic branch on the full benchmark suite:

```console
$ tailfin run bench/bench.sh --build=main --build=branch --default
```

This will take a few minutes.
Results from the full benchmark suite can be seen in performance-related pull requests, for example [#126].

[#126]: https://github.com/tavianator/bfs/pull/126
