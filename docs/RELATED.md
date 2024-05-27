# Related utilities

There are many tools that can be used to find files.
This is a catalogue of some of the most important/interesting ones.

## `find`-compatible

### System `find` implementations

These `find` implementations are commonly installed as the system `find` utility in UNIX-like operating systems:

- [GNU findutils](https://www.gnu.org/software/findutils/) ([manual](https://www.gnu.org/software/findutils/manual/html_node/find_html/index.html), [source](https://git.savannah.gnu.org/cgit/findutils.git))
- BSD `find`
  - FreeBSD `find` ([manual](https://www.freebsd.org/cgi/man.cgi?find(1)), [source](https://cgit.freebsd.org/src/tree/usr.bin/find))
  - OpenBSD `find` ([manual](https://man.openbsd.org/find.1), [source](https://cvsweb.openbsd.org/src/usr.bin/find/))
  - NetBSD `find` ([manual](https://man.netbsd.org/find.1), [source](http://cvsweb.netbsd.org/bsdweb.cgi/src/usr.bin/find/))
- macOS `find` ([manual](https://ss64.com/osx/find.html), [source](https://github.com/apple-oss-distributions/shell_cmds/tree/main/find))
- Solaris `find`
  - [Illumos](https://illumos.org/) `find` ([manual](https://illumos.org/man/1/find), [source](https://github.com/illumos/illumos-gate/blob/master/usr/src/cmd/find/find.c))

### Alternative `find` implementations

These are not usually installed as the system `find`, but are designed to be `find`-compatible

- [`bfs`](https://tavianator.com/projects/bfs.html) ([manual](https://man.archlinux.org/man/bfs.1), [source](https://github.com/tavianator/bfs))
- [schilytools](https://codeberg.org/schilytools/schilytools) `sfind` ([source](https://codeberg.org/schilytools/schilytools/src/branch/master/sfind))
- [BusyBox](https://busybox.net/) `find` ([manual](https://busybox.net/downloads/BusyBox.html#find), [source](https://git.busybox.net/busybox/tree/findutils/find.c))
- [ToyBox](https://landley.net/toybox/) `find` ([manual](http://landley.net/toybox/help.html#find), [source](https://github.com/landley/toybox/blob/master/toys/posix/find.c))
- [Heirloom Project](https://heirloom.sourceforge.net/) `find` ([manual](https://heirloom.sourceforge.net/man/find.1.html), [source](https://github.com/eunuchs/heirloom-project/blob/master/heirloom/heirloom/find/find.c))
- [uutils](https://uutils.github.io/) `find` ([source](https://github.com/uutils/findutils))

## `find` alternatives

These utilities are not `find`-compatible, but serve a similar purpose:

- [`fd`](https://github.com/sharkdp/fd): A simple, fast and user-friendly alternative to 'find'
- `locate`
  - [GNU `locate`](https://www.gnu.org/software/findutils/locate)
  - [`mlocate`](https://pagure.io/mlocate) ([manual](), [source](https://pagure.io/mlocate/tree/master))
  - [`plocate`](https://plocate.sesse.net/) ([manual](https://plocate.sesse.net/plocate.1.html), [source](https://git.sesse.net/?p=plocate))
- [`walk`](https://github.com/google/walk): Plan 9 style utilities to replace find(1)
- [fselect](https://github.com/jhspetersson/fselect): Find files with SQL-like queries
- [rawhide](https://github.com/raforg/rawhide): find files using pretty C expressions
