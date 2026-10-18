# Releases and the update repository

> [한국어](README.ko.md)

The Linux build is released by Meo-hong, separately from the original LLCV
project. Version 2.1.3.1 is published on the
[Releases](https://github.com/Meo-Hong/LLCV-for-Linux/releases) page, and the
`.deb` packages update through the APT repository at
`https://meo-hong.github.io/LLCV-for-Linux/` (GitHub Pages, suite `stable`),
signed with "LLCV Archive Signing Key <busimh001@gmail.com>". The publishing
steps below are already done; for a new version see
[New version](#new-version).

## What is ready

| File | Role |
| --- | --- |
| `apt/repository.conf` | Repository URL (`https://meo-hong.github.io/LLCV-for-Linux/`), suite (`stable`), Origin, Label, Component |
| `apt/llcv.sources.in` | APT source template installed by the package (deb822); `Architectures` is set to the package architecture so multiarch systems do not ask for i386 |
| `../debian/rules` | Adds `/etc/apt/sources.list.d/llcv.sources` and `/etc/apt/keyrings/llcv-archive-keyring.asc` only when the URL and public key exist |
| `../tools/install-build-deps.sh` | Detects the distribution and installs the build dependencies (apt, dnf or pacman) |
| `../tools/build-package.sh` | Builds the package for the running distribution and prints the install command |
| `../tools/build-deb.sh` | Builds the `.deb` for the running release into `dist/<distro>-<version>/` |
| `../tools/build-deb-docker.sh` | Builds the `.deb` for another release in Docker; a second argument (`arm64`, `armhf`) cross-compiles for ARM |
| `../tools/build-arch-docker.sh` | Builds the Arch package (`PKGBUILD`) in an `archlinux` container |
| `rpm/llcv.spec` | RPM package for Fedora (system SDL3) and RHEL/Rocky/AlmaLinux (bundled SDL3) |
| `../tools/build-rpm.sh` | Builds the `.rpm` on a Fedora or EL host |
| `../tools/build-rpm-docker.sh` | Builds the `.rpm` in Docker (`fedora:latest`, `almalinux:9`, `almalinux:10`) |
| `../tools/collect-release.sh` | Copies the installable packages from `dist/*/` into `dist/release/llcv-<version>/` with `SHA256SUMS`, leaving out debug, source and build-record files |
| `../tools/build-apt-repo.sh` | Puts every `.deb` from `dist/*/` into the one `stable` suite (one package per architecture), and signs it when a key is given |
| `../../.github/workflows/linux-packages.yml` | GitHub Actions: the release packages (amd64 `.deb` on Ubuntu 22.04, arm64 on Debian 12, armhf on Debian 13, `.rpm` on AlmaLinux 9, Arch package), signed repository on GitHub Pages, GitHub release |
| `arch/PKGBUILD` | Arch package (usable for the AUR) |

Packages register the repository because the public key
`apt/llcv-archive-keyring.asc` is in the source tree.

## Publishing steps (done)

1. **Create the signing key**
   ```sh
   gpg --quick-generate-key "LLCV Archive Signing Key <email>" ed25519 sign 3y
   gpg --armor --export <key id> > linux/packaging/apt/llcv-archive-keyring.asc
   gpg --armor --export-secret-keys <key id>
   ```
   Keep the secret key out of the repository; store it only in GitHub Secrets.
2. **Repository URL (set)**
   `apt/repository.conf` points to `https://meo-hong.github.io/LLCV-for-Linux/`
   with the suite `stable`. Every supported release uses the same suite, since
   one package per architecture covers them all.
3. **GitHub settings**
   - Secrets: `LLCV_APT_PRIVATE_KEY` (the whole secret key), `LLCV_APT_KEY_ID`
   - Settings → Pages → Source: GitHub Actions
   - Settings → Environments → `github-pages` → add a deployment tag rule for `linux-v*`
4. **Release**
   Push a tag that starts with `linux-v`, such as `linux-v2.1.3.1`. The
   workflow builds the packages, installs each `.deb` in clean containers
   (Ubuntu 22.04, 24.04 and 26.04 and Debian 12 and 13 for amd64, Debian 12
   and 13 for arm64, Debian 13 for armhf) and runs it, then signs and deploys
   the APT repository to Pages and creates a GitHub release with the five
   packages and `SHA256SUMS`. A failed install check stops both. Locally,
   `tools/test-deb-install.sh <image> <arch> <file.deb>` runs the same check.
5. **Arch (optional)**
   For the AUR, change the build path in `arch/PKGBUILD` to download the
   release tarball.

## New version

1. Raise the version in `linux/CMakeLists.txt`, `debian/changelog`,
   `rpm/llcv.spec`, `arch/PKGBUILD`, `data/io.github.meo_hong.LLCV.metainfo.xml`
   and `data/llcv.1`, and in the file names of the READMEs and BUILDING guides.
2. Write the update notes in `linux/release-notes/v<version>.md`, Korean then
   English. The release job puts this file at the top of the GitHub release
   text, followed by the download table. Without the file, the release shows
   only the table.
3. Commit and push `main`.
4. Push a tag such as `linux-v2.1.3.1` on that commit. GitHub Actions builds the
   five packages, updates the APT repository and creates the release. The
   package version comes from `debian/changelog` and the spec, not from the
   tag name, so keep them the same. Re-running the job for an existing release
   replaces its files and its text.

```sh
git tag linux-v2.1.3.1
git push origin linux-v2.1.3.1
```

## On user machines

- The first manual `.deb` install registers the repository and public key.
  New versions then arrive through `apt upgrade` or Software Updater.
- `apt remove llcv` keeps the repository configuration; `apt purge llcv`
  removes it.
- Users who want fully automatic installs opt in themselves:
  ```sh
  echo 'Unattended-Upgrade::Origins-Pattern:: "origin=LLCV";' | sudo tee /etc/apt/apt.conf.d/52llcv-unattended-upgrades
  ```

## Release packages

A package built on an older release installs on newer ones, so each format is
built once, on the oldest supported release. File names carry no
distribution name.

| File | Built on | Installs on |
| --- | --- | --- |
| `llcv_<version>_amd64.deb` | Ubuntu 22.04 | Ubuntu 22.04 to 26.04, Debian 12 and 13, Mint, Pop!_OS, Zorin, elementary, LMDE, Kali |
| `llcv_<version>_arm64.deb` | Debian 12 | Debian 12 and 13, Raspberry Pi OS 64-bit (bookworm, trixie) |
| `llcv_<version>_raspberrypi-32bit_armhf.deb` | Debian 13 | Debian 13, Raspberry Pi OS 32-bit (trixie) |
| `llcv-<version>-1.x86_64.rpm` | AlmaLinux 9 | RHEL, Rocky, AlmaLinux 9 and 10, Fedora, Nobara |
| `llcv-<version>-1-x86_64.pkg.tar.zst` | Arch Linux | Arch, Manjaro, EndeavourOS |

```sh
rm -rf dist
./tools/build-deb-docker.sh ubuntu:22.04
./tools/build-deb-docker.sh debian:12 arm64
./tools/build-deb-docker.sh debian:13 armhf
./tools/build-rpm-docker.sh almalinux:9
./tools/build-arch-docker.sh
./tools/collect-release.sh
```

The files and `SHA256SUMS` end up in `dist/release/llcv-<version>/`; the
`armhf` package is renamed there so the name shows it is for 32-bit Raspberry
Pi OS. The
`armhf` package is built on Debian 13 because 32-bit ARM libraries changed
their ABI in Debian 13 (64-bit `time_t`); it does not install on Raspberry Pi
OS bookworm.

## Local check before publishing

```sh
./tools/build-deb.sh
./tools/build-deb-docker.sh ubuntu:24.04
./tools/build-apt-repo.sh
```

Without a key this writes an unsigned repository to `dist/apt-repo/`. It is
for checking the layout only and is not published.
