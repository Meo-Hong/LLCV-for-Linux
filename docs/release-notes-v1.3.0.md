## v1.3.0

### 한국어

- P010 HDR10 입력 처리를 정리하고, 색 정보가 없는 입력에 PQ/BT.2020 기본 해석을 적용했습니다. HDR 강제 옵션은 색 정보가 잘못 보고되는 입력에 사용할 수 있습니다.
- 지원되는 AVerMedia·Elgato 장치에서 캡처 형식에 맞춰 내부 HDR→SDR 톤매핑을 요청하도록 개선했습니다. 선택한 장치와 제어 기능의 지원 여부를 확인한 뒤 시작 시 적용합니다.
- HDR 출력 조합 사전 검사와 실행 중 영상·오디오 형식 변경 검사를 강화하고, 설정 확인 및 재시작 안내를 다듬었습니다.
- 창·출력 재설정 뒤 최신 입력 프레임을 다시 선택하여 화면 갱신을 개선했습니다.
- MJPEG 디코더 선택, 출력 형식 변경 처리와 저지연 속성 요청을 개선했습니다.
- WASAPI Exclusive 초기화·이벤트 출력과 ASIO 정수 PCM 출력을 다듬었습니다.
- 기존 최신 프레임 우선 처리와 오디오 버퍼 기본값을 유지합니다. 추가 프레임 큐나 외부 프레임워크·런타임은 없습니다.

P010 HDR10과 콘솔 LPCM 5.1은 계속 실험적 기능입니다. HDR10 사용 시 입력 기기와 표시 모니터의 Windows HDR을 켜 주세요. 장치별 지원 범위와 설정은 [영상 안내](https://github.com/seria-aa/LowLatencyCaptureViewer/blob/v1.3.0/docs/VIDEO.ko.md)를 참고해 주세요.

### English

- Refined P010 HDR10 input handling, using PQ/BT.2020 defaults when input color information is absent. Force HDR10 remains available for incorrectly reported color information.
- Added capture-format-aware HDR-to-SDR hardware tone-mapping requests for supported AVerMedia and Elgato devices, after checking the selected device and control support at startup.
- Strengthened HDR output preflight and runtime video/audio format validation, with clearer settings and restart guidance.
- Refreshes the latest input frame after window/output resets.
- Improved MJPEG decoder selection, output-format changes, and optional low-latency requests.
- Refined WASAPI Exclusive initialization/event rendering and ASIO integer PCM output.
- Retains latest-frame-first processing and existing audio-buffer defaults, without additional frame queues, external frameworks, or runtimes.

P010 HDR10 and console LPCM 5.1 remain experimental. For HDR10, enable HDR on the source and Windows HDR on the viewing display. See the [video guide](https://github.com/seria-aa/LowLatencyCaptureViewer/blob/v1.3.0/docs/VIDEO.md) for device-specific scope and settings.
