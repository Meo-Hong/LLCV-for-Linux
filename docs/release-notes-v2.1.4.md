# LLCV v2.1.4

## 한국어

설정 접근과 녹화 안정성을 다듬은 수정 릴리스입니다.

- **F2**는 기본 설정 화면을 엽니다. 녹화 옵션은 **녹화** 탭에서 선택하며, 녹화 중 기본 설정으로 전환할 때는 녹화를 마무리할지 먼저 확인합니다.
- 오디오를 큰 묶음으로 전달하는 일부 장치의 **녹화 입력 처리**를 보완했습니다.
- 녹화 종료와 입력이 겹칠 때 **마지막 영상·오디오 데이터가 누락될 수 있던 문제**를 수정했습니다.
- 설정 파일의 잘못된 숫자값을 기본값으로 처리하고, 좌우 음량의 범위 검사를 보완했습니다.

기존 설치 경로·설정·사용자 지정 단축키와 기본 녹화 상한 **1080p·60fps**는 유지합니다.
재생 음소거와 녹화 음량은 계속 별도로 동작합니다.

[사용 가이드](USAGE.ko.md) · [녹화 안내](RECORDING.ko.md)

## English

A maintenance release focused on settings access and recording reliability.

- **F2** opens the default settings page. Select the **Recording** tab for recording options; switching to general settings while recording first asks whether to finish saving.
- Improved **recording input handling** for devices that deliver audio in larger packets.
- Fixed possible **loss of the last video/audio packets** when input delivery overlaps recording shutdown.
- Invalid numeric settings now fall back to defaults, with safer range handling for left/right volume.

Existing installation paths, settings, custom shortcuts and default recording limits of **1080p/60fps** are retained.
Playback mute and recording volume remain independent.

[User guide](USAGE.md) · [Recording guide](RECORDING.md)
