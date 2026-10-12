## v2.0.4

### 한국어

40fps 캡처 옵션을 추가했습니다. 필요한 경우에만 켜는 선택 옵션이며, 일반적인 시청에는 기존 프레임 설정을 그대로 사용하면 됩니다.

#### 40fps 캡처

- 영상 설정 왼쪽 하단에 **40fps 캡처** 체크박스를 추가했습니다. 기본값은 꺼짐입니다. 체크하면 기존 프레임 선택 대신 40fps를 요청하고, 해제하면 기존 선택으로 돌아갑니다.
- 자동 인식 목록에 없는 40fps도 요청할 수 있습니다. 지원 여부는 캡처 장치·해상도·픽셀 포맷에 따라 다르며, **시작**할 때 드라이버가 수락하고 연결 형식을 40fps로 보고하는지 확인합니다.
- 드라이버가 거부하거나 다른 프레임으로 연결하면 시작 오류를 안내합니다. 다른 FPS로 자동 대체하지 않으며, 옵션을 해제하면 기존 설정으로 다시 시도할 수 있습니다. 실제 수신 속도는 Tab 진단창에서도 확인하세요.
- 프레임을 보간하거나 게임을 40fps로 고정하는 기능이 아니라, 캡처 드라이버에 해당 속도를 요청하는 옵션입니다. VRR을 대체하지 않으며, 40fps 게임을 보기 위해 반드시 켜야 하는 옵션도 아닙니다.

기존 설정과 설치 경로, 실행 파일 이름은 유지합니다. 오디오 기본값과 최신 프레임 우선 표시 정책도 바뀌지 않습니다.
실제 지원 여부는 사용 환경에 따라 다릅니다. 자세한 조건은 [40fps 캡처 안내](VIDEO.ko.md#40fps-캡처), 시작 실패 시에는 [문제 해결](TROUBLESHOOTING.ko.md#40fps-캡처가-시작되지-않음)을 참고하세요.

### English

Added optional 40 fps capture. Enable it only when needed; normal viewing can keep the existing frame-rate selection.

#### 40 fps capture

- Added **40 fps capture** at the bottom-left of Video settings. It is off by default. Enabling it requests 40 fps instead of the normal frame-rate selection; disabling it restores that selection.
- A 40 fps request can be tried even when it is absent from the detected modes. Support depends on the device, resolution, and pixel format. On **Start**, the app checks that the driver accepts the request and reports a 40 fps connected format.
- A rejected request or a different connected rate produces a startup error, without silently falling back to another FPS. Disable the option to retry normal capture. Also check the measured input rate in the Tab overlay.
- This requests a capture-driver rate; it does not interpolate frames, lock a game to 40 fps, or replace VRR. It is not required just to view a 40 fps game.

Existing settings, installation paths, and the executable filename are retained. Audio defaults and latest-frame-first presentation are unchanged.
Device support remains environment-dependent. See the [40 fps capture guide](VIDEO.md#40-fps-capture) and [startup troubleshooting](TROUBLESHOOTING.md#40-fps-capture-will-not-start).
