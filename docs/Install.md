
# Installation

## Packages

<br>

### ![Badge Alpine]

```sh
apk add bfs
```

<br>

### ![Badge Debian] ![Badge Ubuntu]

```sh
apt install bfs
```

<br>

### ![Badge NixOS]

```sh
nix-env -i bfs
```

<br>

### ![Badge Void]

```sh
xbps-install -S bfs
```

<br>

### ![Badge FreeBSD]

```sh
pkg install bfs
```

<br>

### ![Badge MacPorts]

```sh
port install bfs
```

<br>

### ![Badge Homebrew]

```sh
brew install tavianator/tap/bfs
```



<details>
<summary><code>bfs</code> may already be packaged for your operating system.</summary>

<pre>

</pre>
</details>

<details>
<summary>To build <code>bfs</code> from source, you may need to install some dependencies.</summary>

The only absolute requirements for building `bfs` are a C compiler, [GNU make](https://www.gnu.org/software/make/), and [Bash](https://www.gnu.org/software/bash/).
These are installed by default on many systems, and easy to install on most others.
Refer to your operating system's documentation on building software.

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

[Badge Alpine]: https://img.shields.io/badge/Alpine_Linux-0D597F?style=for-the-badge&logo=alpine-linux&logoColor=white
[Badge Ubuntu]: https://img.shields.io/badge/Ubuntu-E95420?style=for-the-badge&logo=ubuntu&logoColor=white
[Badge Debian]: https://img.shields.io/badge/Debian-A81D33?style=for-the-badge&logo=debian&logoColor=white
[Badge Homebrew]: https://img.shields.io/badge/Homebrew-FBB040?style=for-the-badge&logo=homebrew&logoColor=white
[Badge NixOS]: https://img.shields.io/badge/NixOS-5277C3?style=for-the-badge&logo=nixos&logoColor=white
[Badge FreeBSD]: https://img.shields.io/badge/FreeBSD-AB2B28?style=for-the-badge&logo=freebsd&logoColor=white
[Badge MacPorts]: https://img.shields.io/badge/MacPorts-gray?style=for-the-badge&logo=apple&logoColor=white
[Badge Void]: https://img.shields.io/badge/VoidLinux-478061?style=for-the-badge&logo=linux&logoColor=white
