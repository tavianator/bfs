# Build

*The following describes how to* <br>
*build this project from source.*

<br>

---

<br>

## Dependencies

- **C Compiler**

- **[GNU Make]**

- **[Bash]**

<br>

*These are usually preinstalled on most* <br>
*systems and easily added if missing.* <br>
⤷ *Refer to your OS's documentation*

<br>

---

<br>

## Libraries

- **[Oniguruma]**

- **[LibCap]** *`Linux Only`*

- **[ATTR]** *`Linux Only`*

- **[ACL]** *`Linux Only`*

<br>

### Optional

*These dependencies are optional and can <br>
be disabled by specifying them in the build <br>
command as shown in the following:*

```sh
make WITH_< Library >=
```

```sh
make WITH_ONIGURUMA=
```

<br>

### Installation

<br>

### ![Badge Arch]

```sh
pacman -S acl attr libcap oniguruma
```

<br>

### ![Badge Alpine]

```sh
apk add acl{,-dev} attr{,-dev} libcap{,-dev} oniguruma-dev
```

<br>

### ![Badge Debian] ![Badge Ubuntu]

```sh
apt install acl libacl1-dev attr libattr1-dev libcap2-bin libcap-dev libonig-dev
```

<br>

### ![Badge Fedora]

```sh
dnf install libacl-devel libattr-devel libcap-devel oniguruma-devel
```

<br>

### ![Badge NixOS]

```sh
nix-env -i acl attr libcap oniguruma
```

<br>

### ![Badge Void]

```sh
xbps-install -S acl-{devel,progs} attr-{devel,progs} libcap-{devel,progs} oniguruma-devel
```

<br>

### ![Badge FreeBSD]

```sh
pkg install oniguruma
```

<br>

### ![Badge MacPorts]

```sh
port install oniguruma6
```

<br>

### ![Badge Homebrew]

```sh
brew install oniguruma
```

<br>

---

<br>

## Steps

1. Download a **[Release]** or clone the **[Repository]**.

2. Compile the project with:

   ```sh
   make
   ```
   
   <br>
   
   ***With Tests:***
   
   ```sh
   make check
   ```
   
   <br>
   
   ***With Speed:***
   
   ```sh
   make release
   ```
   
   <br>
   
3. Optionally, install it globally with:

   ```sh
   make install
   ```
   
<br>

---

<br>

## Details

*Check our **[Makefile]** documentation* <br>
*for more ways to adjust your build.*

<!----------------------------------------------------------------------------->

[GNU Make]: https://www.gnu.org/software/make/
[Bash]: https://www.gnu.org/software/bash/


[Oniguruma]: https://github.com/kkos/oniguruma 
[libcap]: https://sites.google.com/site/fullycapable/
[attr]: https://savannah.nongnu.org/projects/attr
[acl]: https://savannah.nongnu.org/projects/acl


[Repository]: https://github.com/tavianator/bfs
[Release]: https://github.com/tavianator/bfs/releases
[Makefile]: Makefile.md

<!----------------------------------{ Badges }--------------------------------->

[Badge Alpine]: https://img.shields.io/badge/Alpine_Linux-0D597F?style=for-the-badge&logo=Alpine-linux&logoColor=white
[Badge Ubuntu]: https://img.shields.io/badge/Ubuntu-E95420?style=for-the-badge&logo=Ubuntu&logoColor=white
[Badge Debian]: https://img.shields.io/badge/Debian-A81D33?style=for-the-badge&logo=Debian&logoColor=white
[Badge Homebrew]: https://img.shields.io/badge/Homebrew-FBB040?style=for-the-badge&logo=Homebrew&logoColor=white
[Badge NixOS]: https://img.shields.io/badge/NixOS-5277C3?style=for-the-badge&logo=NixOS&logoColor=white
[Badge FreeBSD]: https://img.shields.io/badge/FreeBSD-AB2B28?style=for-the-badge&logo=FreeBSD&logoColor=white
[Badge MacPorts]: https://img.shields.io/badge/MacPorts-gray?style=for-the-badge&logo=Apple&logoColor=white
[Badge Void]: https://img.shields.io/badge/VoidLinux-478061?style=for-the-badge&logo=Linux&logoColor=white
[Badge Fedora]: https://img.shields.io/badge/Fedora-51A2DA?style=for-the-badge&logo=Fedora&logoColor=white
[Badge Arch]: https://img.shields.io/badge/Arch_Linux-1793D1?style=for-the-badge&logo=ArchLinux&logoColor=white
