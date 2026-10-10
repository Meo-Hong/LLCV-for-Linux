# 배포 준비 상태 (업데이트 저장소)

> [English](README.md)

LLCV 리눅스판은 APT 저장소로 업데이트를 받도록 준비되어 있습니다. 서명 키와
저장소 주소는 원 제작자와 협의한 뒤 정하기로 했으므로, 아직 아무것도 배포되지
않았고 키도 만들지 않았습니다. 아래 항목만 채우면 바로 배포할 수 있습니다.

## 준비된 것

| 파일 | 역할 |
| --- | --- |
| `apt/repository.conf` | 저장소 주소, Origin, Label, Component. 주소(`LLCV_APT_URL`)가 비어 있음 |
| `apt/llcv.sources.in` | 패키지가 설치할 APT 소스 템플릿 (deb822 형식) |
| `../debian/rules` | 주소와 공개 키가 있을 때만 `/etc/apt/sources.list.d/llcv.sources`와 `/etc/apt/keyrings/llcv-archive-keyring.asc`를 패키지에 넣음 |
| `../tools/install-build-deps.sh` | 배포판을 알아내 빌드 의존성 설치 (apt, dnf, pacman) |
| `../tools/build-package.sh` | 지금 배포판용 패키지를 만들고 설치 명령 표시 |
| `../tools/build-deb.sh` | 지금 릴리스용 `.deb`를 빌드해 `dist/<배포판>-<버전>/`에 저장 |
| `../tools/build-deb-docker.sh` | 다른 릴리스용 `.deb`를 Docker에서 빌드. 두 번째 인자(`arm64`, `armhf`)로 ARM 교차 컴파일 |
| `../tools/build-arch-docker.sh` | Arch 패키지(`PKGBUILD`)를 `archlinux` 컨테이너에서 빌드 |
| `rpm/llcv.spec` | Fedora(시스템 SDL3)와 RHEL/Rocky/AlmaLinux(번들 SDL3)용 RPM 패키지 |
| `../tools/build-rpm.sh` | Fedora 또는 EL에서 `.rpm` 빌드 |
| `../tools/build-rpm-docker.sh` | Docker에서 `.rpm` 빌드 (`fedora:latest`, `almalinux:9`, `almalinux:10`) |
| `../tools/collect-release.sh` | `dist/*/`에서 설치용 패키지만 모아 `dist/release/llcv-<버전>/`에 복사하고 `SHA256SUMS`를 만듦. 디버그·소스·빌드 기록 파일은 뺌 |
| `../tools/build-apt-repo.sh` | `dist/*/`의 `.deb`로 저장소 트리 생성. 키가 있으면 서명 |
| `ci/linux-packages.yml` | GitHub Actions: Ubuntu 22.04/24.04/26.04, Debian 12/13 `.deb`(arm64/armhf 포함), Fedora/EL `.rpm`, Arch 패키지, 서명된 저장소를 GitHub Pages에 배포 |
| `arch/PKGBUILD` | Arch 패키지 (AUR 등록 시 그대로 사용 가능) |

키와 주소가 없는 지금 상태로 빌드하면, 패키지는 저장소를 등록하지 않습니다.
일반 `.deb`와 똑같이 동작합니다.

## 배포할 때 할 일

1. **서명 키 만들기 (원 제작자 또는 협의된 담당자)**
   ```sh
   gpg --quick-generate-key "LLCV Archive Signing Key <메일 주소>" ed25519 sign 3y
   gpg --armor --export <키 ID> > linux/packaging/apt/llcv-archive-keyring.asc
   gpg --armor --export-secret-keys <키 ID>
   ```
   비밀 키는 저장소에 올리지 말고 GitHub Secrets에만 넣습니다.
2. **저장소 주소 정하기**
   `apt/repository.conf`의 `LLCV_APT_URL`에 적습니다. GitHub Pages라면
   `https://<계정>.github.io/<저장소>/` 형식입니다.
3. **GitHub 설정**
   - Secrets: `LLCV_APT_PRIVATE_KEY`(비밀 키 전체), `LLCV_APT_KEY_ID`(키 ID)
   - Settings → Pages → Source: GitHub Actions
   - `ci/linux-packages.yml`을 저장소 루트의 `.github/workflows/`로 복사
4. **릴리스**
   `linux-v2.0.2`처럼 `linux-v`로 시작하는 태그를 푸시하면, 빌드부터 서명,
   Pages 배포까지 진행됩니다.
5. **Arch (선택)**
   AUR에 올리려면 `arch/PKGBUILD`의 빌드 경로를 릴리스 tarball을 받는 방식으로
   바꿔서 등록합니다.

## 사용자 쪽 동작

- 처음 한 번 `.deb`를 설치하면 저장소와 공개 키가 등록됩니다. 그 뒤로는
  `apt upgrade`나 소프트웨어 업데이트에 새 버전이 나타납니다.
- `apt remove llcv`를 해도 저장소 설정은 남습니다. `apt purge llcv`를 하면
  저장소 설정까지 지워집니다.
- 설치까지 자동으로 하고 싶은 사용자는 직접 설정합니다:
  ```sh
  echo 'Unattended-Upgrade::Origins-Pattern:: "origin=LLCV";' | sudo tee /etc/apt/apt.conf.d/52llcv-unattended-upgrades
  ```

## 배포용 패키지

옛 릴리스에서 만든 패키지는 새 릴리스에도 설치되므로, 형식마다 가장 오래된 지원
릴리스에서 한 번만 빌드합니다. 파일 이름에는 배포판 이름을 넣지 않습니다.

| 파일 | 빌드한 곳 | 설치되는 곳 |
| --- | --- | --- |
| `llcv_<버전>_amd64.deb` | Ubuntu 22.04 | Ubuntu 22.04~26.04, Debian 12·13, Mint, Pop!_OS, Zorin, elementary, LMDE, Kali |
| `llcv_<버전>_arm64.deb` | Debian 12 | Debian 12·13, Raspberry Pi OS 64비트 (bookworm, trixie) |
| `llcv_<버전>_raspberrypi-32bit_armhf.deb` | Debian 13 | Debian 13, Raspberry Pi OS 32비트 (trixie) |
| `llcv-<버전>-1.x86_64.rpm` | AlmaLinux 9 | RHEL, Rocky, AlmaLinux 9·10, Fedora, Nobara |
| `llcv-<버전>-1-x86_64.pkg.tar.zst` | Arch Linux | Arch, Manjaro, EndeavourOS |

```sh
rm -rf dist
./tools/build-deb-docker.sh ubuntu:22.04
./tools/build-deb-docker.sh debian:12 arm64
./tools/build-deb-docker.sh debian:13 armhf
./tools/build-rpm-docker.sh almalinux:9
./tools/build-arch-docker.sh
./tools/collect-release.sh
```

파일과 `SHA256SUMS`는 `dist/release/llcv-<버전>/`에 모입니다. 이때 `armhf`
패키지는 32비트 Raspberry Pi OS용임이 보이도록 이름을 바꿔 복사합니다. `armhf`만 Debian
13에서 빌드하는 이유는 32비트 ARM 라이브러리의 ABI가 Debian 13에서 바뀌었기
때문입니다(64비트 `time_t`). 그래서 이 패키지는 Raspberry Pi OS bookworm에는
설치되지 않습니다.

## 배포 전 로컬 확인

```sh
./tools/build-deb.sh
./tools/build-deb-docker.sh ubuntu:24.04
./tools/build-apt-repo.sh
```

키 없이 실행하면 서명되지 않은 저장소가 `dist/apt-repo/`에 만들어집니다.
구조만 확인하는 용도이고, 이 상태로는 배포하지 않습니다.
