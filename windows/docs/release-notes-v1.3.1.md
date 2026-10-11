## v1.3.1

### 한국어

- **F12 스크린샷**을 추가했습니다. 입력 해상도의 PNG를 사진 폴더의 `LowLatencyCaptureViewer`에 저장합니다.
- NV12·YUY2·MJPEG 촬영과 P010 HDR10의 **SDR 변환 저장**을 지원합니다. OSD와 화면 필터는 포함하지 않습니다.
- **영상·창 설정 최하단**에 클립보드 동시 복사 옵션과 저장 폴더 열기를 추가했습니다. 변환과 저장은 백그라운드에서 한 장씩 처리합니다.
- **F1 안내창**에서 앱 버전, 키보드 단축키, 마우스 조작, 스크린샷 안내를 섹션별로 확인할 수 있습니다.
- 안내창 색상을 오디오 전용 화면과 통일하고, 창 크기에 맞춰 배치를 조절하도록 다듬었습니다.

HDR 스크린샷은 일반 이미지로 사용할 수 있도록 SDR로 톤 매핑하며, HDR 원본 보존 저장은 아닙니다. 필요한 사진은 저장 완료 안내를 확인한 뒤 앱을 종료해 주세요.

### English

- Added **F12 screenshots**, saving source-resolution PNGs to `Pictures / LowLatencyCaptureViewer`.
- Supports NV12, YUY2, decoded MJPEG, and **SDR-converted P010 HDR10 screenshots**, without OSD or display filters.
- Added optional clipboard copying and an open-folder button at the **bottom of Video & window settings**. Conversion and saving run in the background, one image at a time.
- Added a sectioned **F1 guide** with the app version, keyboard shortcuts, mouse controls, and screenshot information.
- Matched the guide palette to the dedicated audio-only screen and refined its layout for different window sizes.

HDR screenshots are tone-mapped to SDR for use as ordinary images; they do not preserve the HDR original. Wait for the save confirmation before closing the app if you need the screenshot.
