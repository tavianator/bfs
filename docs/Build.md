## Build

### Dependencies

- **C Compiler**

- **[GNU Make]**

- **[Bash]**

<br>

*These are usually preinstalled on most* <br>
*systems and easily added if missing.* <br>
⤷ *Refer to your OS's documentation*

<br>

### Libraries

- 


`bfs` also depends on some system libraries for some of its features.

These dependencies are optional, and can be turned off at build time if necessary by setting the appropriate variable to the empty string (e.g. `make WITH_ONIGURUMA=`).

| Dependency                                            | Platforms  | `make` flag      |
|-------------------------------------------------------|------------|------------------|
| [acl](https://savannah.nongnu.org/projects/acl)       | Linux only | `WITH_ACL`       |
| [attr](https://savannah.nongnu.org/projects/attr)     | Linux only | `WITH_ATTR`      |
| [libcap](https://sites.google.com/site/fullycapable/) | Linux only | `WITH_LIBCAP`    |
| [Oniguruma](https://github.com/kkos/oniguruma)        | All        | `WITH_ONIGURUMA` |

Here's how to install them on some common platforms:

<pre>
<strong>Alpine Linux</strong>
# apk add acl{,-dev} attr{,-dev} libcap{,-dev} oniguruma-dev

<strong>Arch Linux</strong>
# pacman -S acl attr libcap oniguruma

<strong>Debian/Ubuntu</strong>
# apt install acl libacl1-dev attr libattr1-dev libcap2-bin libcap-dev libonig-dev

<strong>Fedora</strong>
# dnf install libacl-devel libattr-devel libcap-devel oniguruma-devel

<strong>NixOS</strong>
# nix-env -i acl attr libcap oniguruma

<strong>Void Linux</strong>
# xbps-install -S acl-{devel,progs} attr-{devel,progs} libcap-{devel,progs} oniguruma-devel

<strong>FreeBSD</strong>
# pkg install oniguruma

<strong>MacPorts</strong>
# port install oniguruma6

<strong>Homebrew</strong>
$ brew install oniguruma
</pre>
</details>

Once the dependencies are installed, download one of the [releases](https://github.com/tavianator/bfs/releases) or clone the [git repo](https://github.com/tavianator/bfs).
Then run

    $ make

This will build the `bfs` binary in the current directory.
Run the test suite to make sure it works correctly:

    $ make check

If you're interested in speed, you may want to build the release version instead:

    $ make release

Finally, if you want to install it globally, run

    # make install


<!----------------------------------------------------------------------------->

[GNU Make]: https://www.gnu.org/software/make/
[Bash]: https://www.gnu.org/software/bash/