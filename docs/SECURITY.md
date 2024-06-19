Security
========

Threat model
------------

`bfs` is a command line program running on multi-user operating systems.
Those other users may be malicious, but `bfs` should not allow them to do anything they couldn't already do.
That includes situations where one user (especially `root`) is running `bfs` on files owned or controlled by another user.

On the other hand, `bfs` implicitly trusts the user running it.
Anyone with enough control over the command line of `bfs` or any `find`-compatible tool can wreak havoc with dangerous actions like `-exec`, `-delete`, etc.

> [!CAUTION]
> The only untrusted input that should *ever* be passed on the `bfs` command line are **file paths**.
> It is *always* unsafe to allow *any* other part of the command line to be affected by untrusted input.
> Use the `-f` flag, or `-files0-from`, to ensure that the input is interpreted as a path.

This still has security implications, incuding:

- **Information disclosure:** an attacker may learn whether particular files exist by observing `bfs`'s output, exit status, or even side channels like execution time.
- **Denial of service:** large directory trees or slow/network storage may cause `bfs` to consume excessive system resources.

> [!TIP]
> When in doubt, do not pass any untrusted input to `bfs`.


Executing commands
------------------

The `-exec` family of actions execute commands, passing the matched paths as arguments.
File names that begin with a dash may be misinterpreted as options, so `bfs` adds a leading `./` in some instances:

```console
user@host$ bfs -execdir echo {} \;
./-rf
```

This might save you from accidentally running `rm -rf` (for example) when you didn't mean to.
This mitigation applies to `-execdir`, but not `-exec`, because the full path typically does not begin with a dash.
But it is possible, so be careful:

```console
user@host$ bfs -f -rf -exec echo {} \;
-rf
```


Race conditions
---------------

Like many programs that interface with the file system, `bfs` can be affected by race conditions&mdash;in particular, "[time-of-check to time-of-use](https://en.wikipedia.org/wiki/Time-of-check_to_time-of-use)" (TOCTTOU) issues.
For example,

```console
user@host$ bfs / -user user -exec dangerous_command {} \;
```

is not guaranteed to only run `dangerous_command` on files you own, because another user may run

```console
evil@host$ mv /path/to/file /path/to/exile
evil@host$ mv ~/malicious /path/to/file
```

in between checking `-user user` and executing the command.

> [!WARNING]
> Be careful when running `bfs` on directories that other users have write access to, because they can modify the directory tree while `bfs` is running, leading to unpredictable results and possible TOCTTOU issues.


Output sanitization
-------------------

In general, printing arbitrary data to a terminal may have [security](https://hdm.io/writing/termulation.txt) [implications](https://dgl.cx/2023/09/ansi-terminal-security#vulnerabilities-using-known-replies).
On many platforms, file paths may be completely arbitrary data (except for NUL (`\0`) bytes).
Therefore, when `bfs` is writing output to a terminal, it will escape non-printable characters:

<pre>
user@host$ touch $'\e[1mBOLD\e[0m'
user@host$ bfs
.
./$'\e[1mBOLD\e[0m'
</pre>

However, this is fragile as it only applies when outputting directly to a terminal:

<pre>
user@host$ bfs | grep BOLD
<strong>BOLD</strong>
</pre>


Code quality
------------

Every correctness issue in `bfs` is a potential security issue, because acting on the wrong path may do arbitrarily bad things.
For example:

```console
root@host# bfs /etc -name passwd -exec cat {} \;
```

should print `/etc/passwd` but not `/etc/shadow`.
`bfs` tries to ensure correct behavior through careful programming practice, an extensive testsuite, and static analysis.

`bfs` is written in C, which is a memory unsafe language.
Bugs that lead to memory corruption are likely to be exploitable due to the nature of C.
We use [sanitizers](https://github.com/google/sanitizers) to try to detect these bugs.
Fuzzing has also been applied in the past, and deploying continuous fuzzing is a work in progress.


Supported versions
------------------

`bfs` comes with [no warranty](/LICENSE), and is maintained by [me](https://tavianator.com/) and [other volunteers](https://github.com/tavianator/bfs/graphs/contributors) in our spare time.
In that sense, there are no *supported* versions.
However, as long as I maintain `bfs` I will attempt to address any security issues swiftly.
In general, security fixes will we part of the latest release, though for significant issues I may backport fixes to older release series.


Reporting a vulnerability
-------------------------

If you think you have found a sensitive security issue in `bfs`, you can [report it privately](https://github.com/tavianator/bfs/security/advisories/new).
Or you can [report it publicly](https://github.com/tavianator/bfs/issues/new); I won't judge you.
