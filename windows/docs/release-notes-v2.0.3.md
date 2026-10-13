## v2.0.3

### 한국어

설정 화면의 반응성과 영상·오디오 처리 효율을 개선하고, 장치 전환과 설정 저장·종료 과정의 안정성을 보완했습니다.

#### 설정 화면

- 기본 켜짐인 '재생 중 자동 절전 방지' 옵션을 추가했습니다. 영상 재생 시 화면과 시스템을 유지하고, 오디오 전용 모드에서는 화면 꺼짐을 허용합니다. 재생 종료·오류 시 해제하며 Windows 전원 설정은 변경하지 않습니다.

- 작은 화면이나 높은 Windows 표시 배율에서도 설정이 화면 밖으로 잘리지 않도록 개선했습니다. 공간이 부족할 때는 스크롤 대신 항목을 페이지로 나눠 표시합니다.
- 캡처 오디오와 WASAPI Shared 지원 정보도 백그라운드에서 조회하고 재사용하여, 옵션을 바꿀 때 반복되는 검사와 대기를 줄였습니다.
- WASAPI와 ASIO 사이를 전환해도 각 모드의 출력 장치와 유효한 버퍼 선택을 유지합니다. ASIO를 거쳐 돌아왔을 때 버퍼 값이 잘못 바뀌던 문제를 수정했습니다.
- ASIO 상태에서 새로고침한 뒤 Shared로 돌아오면, 이전에 선택한 WASAPI 장치의 지원 정보를 올바르게 갱신합니다.

#### 영상·오디오

- ASIO 재생 시작과 출력 재시작 시 저장된 음량·음소거를 첫 오디오부터 적용하도록 수정했습니다.
- ASIO를 열지 못해 Shared로 전환한 경우에도 사용자가 선택한 ASIO 설정은 유지하고, 진단에는 실제 출력 모드를 표시합니다.
- MJPEG 디코딩 버퍼를 재사용하고, 창 크기 변경 시 필요한 영상 출력 자원만 갱신하여 반복 할당과 재생성 비용을 줄였습니다.
- 오디오 처리 중 진단 메시지를 직접 출력하지 않도록 분리하고, 오류 이력 기록의 메모리 할당을 줄였습니다.

#### 안정성

- 캡처 장치 분리나 입력 경로 오류를 감지하면 재생을 중단하고 재시작 방법을 안내합니다. 장치를 자동으로 반복 검색하지 않습니다.
- 캡처 소스가 모든 스트림의 종료를 알리면 재생과 절전 방지 요청을 정리하고 설정으로 돌아갑니다. 일시적인 무신호는 종료로 처리하지 않습니다.
- 스트림 종료가 창 메시지 처리와 겹쳐도 현재 음량과 창 위치를 저장한 뒤 종료하도록 보완했습니다.
- 수동 절전 후 사용자가 돌아와 재생이 계속될 때 절전 방지를 다시 적용합니다. Modern Standby 알림도 등록하며, 자동 복귀나 중복 알림으로 요청을 반복 등록하지 않습니다.
- 설정 저장에 실패해도 기존 설정 파일을 보존하도록 개선하고, 저장 실패를 안내합니다.
- 업데이트 확인 중 창을 닫거나 앱을 종료할 때 요청 취소와 자원 정리를 개선했습니다.

#### 설치·문서

- 제거 프로그램에서 사용자 데이터 삭제를 선택했을 때, 실행 계정과 대상 경로를 확인하여 확인할 수 없는 데이터는 보존하도록 보완했습니다.
- 설치판과 포터블에 사용 가이드·이미지·관련 문서가 빠짐없이 포함되도록 패키지 검사를 추가했습니다.
- 실행 안내의 문서 경로를 수정하고, 일반 텍스트 안내에 적힌 가이드 경로도 배포 검사에 포함했습니다.
- 사용하지 않는 이미지와 개발·검증 자료는 배포본에서 제외했습니다. 사용 가이드와 필요한 이미지는 유지하며, 개발 기록은 문서 목록의 GitHub 링크로 확인할 수 있습니다.
- 사용하지 않는 코드와 중복 처리를 정리하고 회귀 검사를 보강했습니다.

기존 설정과 설치 경로, 실행 파일 이름은 그대로 사용합니다. 최신 프레임 우선 표시와 오디오 기본값도 유지합니다.
VSR·HDR·ASIO·LPCM 5.1의 사용 조건은 기존과 같습니다. 자세한 내용은 [사용 가이드](USAGE.ko.md)를 참고하세요.

### English

This update improves settings responsiveness and video/audio processing efficiency, with fixes for device transitions, settings persistence, and shutdown.

#### Settings

- Added "Prevent sleep during playback", enabled by default. Video requests that the screen and system stay awake; audio-only allows the screen to turn off. Requests are released when playback ends or fails, without changing Windows power settings.

- Kept settings within the available desktop area on small screens and at high Windows display scales. When space is limited, options are split into pages rather than adding scrollbars.
- Moved capture-audio and WASAPI Shared capability queries to background workers and reused their results, reducing repeated checks and waits when changing options.
- Preserved each backend's output-device choice and valid buffer settings across WASAPI/ASIO switches. Fixed incorrect buffer values after switching through ASIO.
- Fixed manual refresh in ASIO mode so returning to Shared refreshes the capabilities of the previously selected WASAPI device.

#### Video and audio

- Applied saved volume and mute from the first ASIO audio buffer at playback start and output restart.
- Preserved the selected ASIO configuration when startup falls back to Shared, while reporting the actual output mode in diagnostics.
- Reused MJPEG decode buffers and updated only the necessary video-output resources on window resize, reducing repeated allocation and recreation.
- Moved diagnostic message output out of the audio-processing path and reduced allocations when recording audio-error history.

#### Reliability

- Stopped playback and provided restart guidance when the capture device disconnects or its input path fails, without repeatedly rescanning devices.
- Ended playback, released power requests, and returned to Settings when the capture source explicitly completes all streams. Temporary loss of input alone does not end playback.
- Preserved the final volume and window placement even when stream completion overlaps UI message processing.
- Restored sleep prevention when the user returns from sleep and playback continues, including registration for Modern Standby notifications, without rearming on unattended wakes or duplicate notifications.
- Preserved the previous settings file on save failure and added a failure notification.
- Improved cancellation and resource cleanup when closing a window or exiting during an update check.

#### Installation and documentation

- Added account and path checks before optional user-data removal during uninstall; data is retained when these checks cannot be completed safely.
- Added package checks to include user guides, images, and linked documents in both the installer and portable distribution.
- Corrected the guide path in the launch notes and included plain-text guide references in package validation.
- Excluded unused images and development/validation records from binary packages. User guides and their images remain available offline; development records are linked on GitHub from the documentation index.
- Removed unused code and duplicate work, and expanded regression coverage.

Existing settings, installation paths, and the executable filename are retained, as are latest-frame-first presentation and audio defaults.
VSR, HDR, ASIO, and LPCM 5.1 requirements are unchanged. See the [user guide](USAGE.md).
