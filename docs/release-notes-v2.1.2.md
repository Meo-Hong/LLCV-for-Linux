# LLCV v2.1.2

## 한국어

녹화 안정성과 설정 동작을 보완한 수정 릴리스입니다.

- 일부 인코더의 지원 여부를 확인하는 중 **설정 창을 닫으면 멈출 수 있던 문제**를 수정했습니다. 확인 실패는 미지원과 구분하며, 설정을 다시 열어 재시도할 수 있습니다.
- **녹화 시작 부분의 짧은 오디오가 누락될 수 있던 문제**를 수정했습니다.
- MJPEG 재생·녹화에서 최종 연결 시 전달되는 **색 범위·색 행렬 정보**가 반영되도록 수정했습니다.
- 녹화 중 **남은 저장 공간**이 갱신되지 않던 문제를 수정했습니다.
- 설정 창의 입력·탐색 키와 녹화 단축키가 충돌하지 않도록 했습니다. 설정 창에서는 **F4를 제외한 F1~F24 조합**만 녹화 단축키로 동작하며, 재생 창에서는 기존 사용자 지정 키를 그대로 사용할 수 있습니다.
- 최소화한 상태로 종료한 뒤 다시 실행할 때 **창 위치가 잘못 복원될 수 있던 문제**를 수정했습니다.

기존 설치 경로·설정·단축키와 기본 녹화 상한 **1080p·60fps**는 유지합니다.

[녹화 안내](RECORDING.ko.md) · [사용 가이드](USAGE.ko.md) · [문제 해결](TROUBLESHOOTING.ko.md)

## English

A maintenance release focused on recording reliability and settings behavior.

- Fixed a possible **hang when closing settings during encoder capability checks**. Failed checks are distinguished from unsupported encoders; reopen settings to retry.
- Fixed possible **loss of a short section of audio at the start of a recording**.
- Applied **color range and matrix metadata reported at the final connection** to MJPEG playback and recording.
- Fixed **free-space information not updating during recording**.
- Prevented recording shortcuts from interfering with settings input and navigation. In settings, recording shortcuts use **F1–F24 combinations except F4**; the viewer continues to accept existing custom bindings.
- Fixed **incorrect window-position restoration after exiting while minimized**.

Existing installation paths, settings, shortcuts and default recording limits of **1080p/60fps** are retained.

[Recording guide](RECORDING.md) · [User guide](USAGE.md) · [Troubleshooting](TROUBLESHOOTING.md)
