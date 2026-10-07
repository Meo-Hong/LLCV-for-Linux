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
| `../tools/build-deb.sh` | 릴리스별 `.deb` 빌드. 버전에 `~ubuntu24.04`처럼 릴리스 이름이 붙음 |
| `../tools/build-deb-docker.sh` | 다른 릴리스용 `.deb`를 Docker에서 빌드 |
| `../tools/build-apt-repo.sh` | `dist/*/`의 `.deb`로 저장소 트리 생성. 키가 있으면 서명 |
| `ci/linux-packages.yml` | GitHub Actions: 24.04/26.04/Debian 13 `.deb`, Arch 패키지, 서명된 저장소를 GitHub Pages에 배포 |
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

## 배포 전 로컬 확인

```sh
./tools/build-deb.sh
./tools/build-deb-docker.sh ubuntu:24.04
./tools/build-apt-repo.sh
```

키 없이 실행하면 서명되지 않은 저장소가 `dist/apt-repo/`에 만들어집니다.
구조만 확인하는 용도이고, 이 상태로는 배포하지 않습니다.
