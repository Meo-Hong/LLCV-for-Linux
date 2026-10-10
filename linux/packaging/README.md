# Release readiness (update repository)

> [한국어](README.ko.md)

The Linux build is ready to receive updates through an APT repository. The
signing key and the repository address will be chosen together with the
original author, so nothing has been published and no key has been created.
Fill in the items below to publish.

## What is ready

| File | Role |
| --- | --- |
| `apt/repository.conf` | Repository URL, Origin, Label, Component. `LLCV_APT_URL` is empty |
| `apt/llcv.sources.in` | APT source template installed by the package (deb822) |
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
| `../tools/build-apt-repo.sh` | Creates the repository tree from `dist/*/`, and signs it when a key is given |
| `ci/linux-packages.yml` | GitHub Actions: Ubuntu 22.04/24.04/26.04 and Debian 12/13 `.deb` (with arm64/armhf), Fedora/EL `.rpm`, Arch package, signed repository on GitHub Pages |
| `arch/PKGBUILD` | Arch package (usable for the AUR) |

Until the key and URL are set, packages do not register any repository. They
behave like a plain `.deb`.

## Publishing steps

1. **Create the signing key (the original author or an agreed maintainer)**
   ```sh
   gpg --quick-generate-key "LLCV Archive Signing Key <email>" ed25519 sign 3y
   gpg --armor --export <key id> > linux/packaging/apt/llcv-archive-keyring.asc
   gpg --armor --export-secret-keys <key id>
   ```
   Keep the secret key out of the repository; store it only in GitHub Secrets.
2. **Choose the repository URL**
   Set `LLCV_APT_URL` in `apt/repository.conf`. For GitHub Pages the form is
   `https://<owner>.github.io/<repository>/`.
3. **GitHub settings**
   - Secrets: `LLCV_APT_PRIVATE_KEY` (the whole secret key), `LLCV_APT_KEY_ID`
   - Settings → Pages → Source: GitHub Actions
   - Copy `ci/linux-packages.yml` to `.github/workflows/` in the repository root
4. **Release**
   Push a tag that starts with `linux-v`, such as `linux-v2.0.2`. The
   workflow builds, signs and deploys to Pages.
5. **Arch (optional)**
   For the AUR, change the build path in `arch/PKGBUILD` to download the
   release tarball.

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
