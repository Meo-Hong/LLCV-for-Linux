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
| `../tools/build-deb.sh` | Builds the `.deb` for the running release; the version gets a suffix such as `~ubuntu24.04` |
| `../tools/build-deb-docker.sh` | Builds the `.deb` for another release in Docker |
| `../tools/build-apt-repo.sh` | Creates the repository tree from `dist/*/`, and signs it when a key is given |
| `ci/linux-packages.yml` | GitHub Actions: 24.04/26.04/Debian 13 `.deb`, Arch package, signed repository on GitHub Pages |
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

## Local check before publishing

```sh
./tools/build-deb.sh
./tools/build-deb-docker.sh ubuntu:24.04
./tools/build-apt-repo.sh
```

Without a key this writes an unsigned repository to `dist/apt-repo/`. It is
for checking the layout only and is not published.
