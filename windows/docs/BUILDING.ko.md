<img align="right" src="images/app-icon.png" width="40" height="40" alt="LLCV">

# 소스에서 빌드

[README](../README.ko.md) · [문서 목록](INDEX.ko.md) · [English](BUILDING.md)

[필요 환경](#필요-환경) · [Release 빌드](#release-빌드) · [설치 프로그램과 포터블 ZIP](#설치-프로그램과-포터블-zip)

<br>

## 필요 환경

- Windows 10/11 x64
- **C++를 사용한 데스크톱 개발**이 설치된 Visual Studio 2022
- Windows SDK와 Windows용 CMake 도구
- 설치 프로그램을 만들 때만 Inno Setup 7

Win32, DirectShow, D3D11, DXGI, MJPEG 디코딩용 Media Foundation과
WASAPI 등 Windows 시스템 API를 사용합니다. FFmpeg나 서드파티 코덱 팩은
필요하지 않습니다.

<br>

## Release 빌드

저장소 루트에서 x64 Visual Studio 개발자 PowerShell을 열고 실행합니다.

```powershell
chcp.com 65001 > $null
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

실행 파일은 다음 위치에 생성됩니다.

```text
build\Release\LowLatencyCaptureViewer.exe
```

Release 구성은 정적 MSVC 런타임을 사용하므로 패키지 실행에 별도 Visual C++
재배포 패키지가 필요하지 않습니다.

2.0.4에는 기존 설정 페이지·장치 전환·저장 실패·스트림 종료·절전 요청 수명 검사에
더해 40fps 설정 복원·형식 협상·색 정보 보존·한영 UI 회귀 검사가 포함됩니다.
일부 파일 저장 테스트는 쓰기·이름 변경이 가능한 임시 폴더가
필요합니다. GPU나 Windows 기능이 없어 건너뛴 검사는 통과한 검사와 구분해 기록하세요.
절전 API 검사는 PC를 실제로 재우지 않으며, 실제 절전·복귀 검증을 대신하지 않습니다.

<br>

## 설치 프로그램과 포터블 ZIP

Release 빌드 뒤 저장소 루트에서 아래 명령을 실행합니다. 다음은 위에서 만든
`build\Release` 실행 파일을 v2.0.4로 패키징하는 예시입니다. 릴리스 스크립트의
기본 경로는 버전별 빌드 폴더이므로 아래 빌드 경로 재정의가 필요합니다.

```powershell
chcp.com 65001 > $null
& "C:\Program Files\Inno Setup 7\ISCC.exe" `
  "--define=BuildDir=..\build\Release" ".\installer\LowLatencyCaptureViewer.iss"
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\package-v1.ps1 `
  -Version 2.0.4 -BuildDir ..\build\Release -OutputDir outputs\v2.0.4
```

생성 파일은 `outputs\v2.0.4\LowLatencyCaptureViewer_v2.0.4_Setup.exe`와
`outputs\v2.0.4\LowLatencyCaptureViewer_v2.0.4_x64.zip`입니다. 다른 버전은
먼저 릴리스 관련 버전 표기를 맞춰야 합니다. ZIP 이름만 바꿔도 실행 파일 버전이
바뀌지는 않습니다. 자세한 절차는 [릴리스 체크리스트](RELEASING.ko.md)를 따릅니다.

두 패키지에는 `LICENSE`, `ASIO-SDK-LICENSE.txt`, `ASIO-HOST-LICENSE.txt`, `PRETENDARD-LICENSE.txt`가
모두 있어야 합니다. 컴퓨터별 `settings.ini`, 진단 로그, 테스트 실행 파일,
PDB와 빌드 폴더는 포함하지 않습니다. 자동 테스트는 실제 장치나 장시간 재생
검증을 대신하지 않으므로 실제 수행한 하드웨어 검사를 별도로 기록합니다.

실행 중 설정과 로그는 사용자별 다음 폴더에 저장됩니다.

```text
%LOCALAPPDATA%\LowLatencyCaptureViewer
```

이전 버전의 실행 파일 옆에 `settings.ini`가 있으면 첫 실행 때 사용자 폴더로
복사하며, 기존 파일은 자동으로 삭제하지 않습니다.

---

[README](../README.ko.md) · [문서 목록](INDEX.ko.md) · [English](BUILDING.md)
