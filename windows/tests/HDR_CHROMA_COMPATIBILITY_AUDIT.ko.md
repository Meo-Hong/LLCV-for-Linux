# P010 HDR 색차 배치 호환성 선택 — 2026-09-16

## 목적과 한계

ezcap 제보의 chroma=6, range=2, primaries=2, matrix=1, transfer=0 튜플은
기존 강제 HDR10에서도 색차 배치 검증에서 거부됐다. 값 6을 7로 자동 치환하지 않고
사용자가 실제 입력을 확인하면서 Left/Top-left 해석을 명시적으로 선택할 수 있게 한다.

Microsoft DXVA 정의에서 6에는 수직 Cb/Cr 정렬 플래그(1)가 없다. 따라서 실제 배치가
7과 같다고 확정할 수 없다. 이 기능은 잘못된/누락된 메타데이터에 대한 해석 재지정이며,
실제 어긋난 Cb/Cr 표본의 재배치·복원 구현이 아니다. 영상이 열리는 것과 실제 배치가
정확한 것은 다르다. 실장치의 가는 색 경계·글자를 기준 화면과 비교해야 한다.

- [DXVA2 색차 플래그 정의](https://learn.microsoft.com/en-us/windows/win32/api/dxva2api/ne-dxva2api-dxva2_videochromasubsampling)
- [DXGI 색공간 정의](https://learn.microsoft.com/en-us/windows/win32/api/dxgicommon/ne-dxgicommon-dxgi_color_space_type)

## 변경

- P010 선택 시 HDR10 강제 아래 `HDR 색차 배치` 표시: 자동 / Top-left / Left.
- 기본/기존 설정은 자동. 자동의 허용 배치 및 거부 정책은 그대로 유지한다.
- 수동 배치는 PQ/BT.2020 판정 이후에만 적용한다. 별도의 Force HDR10 없이
  SDR/미확인 입력을 HDR로 바꾸지 않는다. Full/지원하지 않는 범위 검증도 유지한다.
- 원래 메타데이터는 수정하지 않는다. 로그에 원본 값·수동 선택·실제 DXGI 배치를 기록한다.
- 진단 프레임 JSON에는 요청한 배치 선택을 별도 기록한다.
- INI 키 `Video/HdrChromaLocation`: Auto / TopLeft / Left. 잘못된 값은 Auto.
  설정 창에서 다른 픽셀 포맷으로 저장하면 선택은 Auto로 초기화한다.
- 한국어/영어 도움말과 분리된 버튼 ID를 사용한다. NV12/YUY2/MJPEG·다른 탭에서는 숨긴다.
- 기존 HDR10 VideoProcessor 경로의 입력 색공간만 초기화 시 선택한다.
  프레임 큐·추가 GPU 패스·CPU 영상 복사·새 라이브러리를 추가하지 않는다.
  실시간 색감 자동 판정이나 채도/밝기 보정은 추가하지 않는다.
- 실험용 vendor 톤매핑 제어는 여전히 OFF. 일반 HDR10 경로를 유지한다.

## 검증

- 정책: 제보 튜플 자동 거부 / 명시적 Top-left·Left 허용 및 assumed 표시;
  원본 메타데이터 보존, 다른 HDR 판정·range 거부 유지, 잘못된 enum 거부.
- 실제 D3D11 GPU(합성 P010, 숨겨진 창): 두 수동 선택이 VideoProcessor의 입력 색공간에
  전달됨을 조회하고 중립 PQ 휘도 유지 확인. scRGB 셰이더에도 같은 정책 결과를 전달해
  Left/Top-left 공간 보간 검증. 디버그 오류/경고 검사 포함.
- UI: 24개 초기 프로필, DPI/탭/픽셀 포맷 조합, 컨트롤 위치/숨김/고유 ID,
  도움말 버튼이 HDR 체크박스나 배치 값을 바꾸지 않음 확인.
- 설정: 모든 선택 저장/복원, 구버전 키 없음, 잘못된 값, 대소문자 검사.
- 실제 캡처 장치를 열지 않는다. 실장치의 색차 위치 정확성/호환성은 미확인이다.

## 테스트 배포

`build-hdr-chroma-test`: Release, F8 진단 ON, scRGB OFF, vendor 제어 OFF.
사용자 설정을 저장하지 않고 자동 업데이트를 끈 비공개 실행 파일이다.
공개 v1.2.8 실행 파일/릴리스는 변경하지 않는다.

## 실행 결과

- 전체 클린 빌드 후 자동 검사 26/26 통과 (97.94초). GPU HDR 검사 5.03초.
- 일반 EXE 647,680바이트: 공개 v1.2.8 643,072바이트보다 4,608바이트 증가.
  새 코드의 per-frame 작업은 없으며 실제 장치의 종단 지연은 측정하지 않았다.
- 진단 EXE 658,432바이트. SHA256:
  `FAFF48FD782479B9FB3518C3D84839259DEE3041314BDB1D0A37E23E9508B86C`.
- `HDR-Chroma-Compatibility-Test.zip` SHA256:
  `A908789128BE56D76EC8075ED93B9867CF5755FF5F6F20445D65ECD7E7578EB3`.
  ZIP 내부 실행 파일과 빌드 실행 파일 해시 일치 확인.
- 진단 실행 파일에 vendor 제어/scRGB 경로 표식이 없고 신규 옵션이 있는지 확인.
- 공개 v1.2.8 EXE SHA256은 기존
  `8EA3846A5D362DD6E276525C83CED0F974FE3F294307C22AEEADD421E0ACEDCB` 유지.

## 추가 회귀 검사 및 HDR Tab 배경 — 2026-09-16

- HDR Tab 정보창은 기존 청회색 대신 순수 검정 RGB(0,0,0), 불투명도 90%로 변경.
  SDR 정보창과 음량/오디오 패널의 기존 테마는 유지. 글자 밝기와 합성 셰이더는 변경하지 않음.
- HDR의 90%는 선형 광량 기준: 배경 영상의 약 10%가 비친다. 밝은 HDR 영상 위에서도
  항상 같은 검정으로 보이게 하는 불투명 패널/별도 톤매핑은 아니다.
  BGRA8 캐시의 알파 양자화(229 또는 230/255)를 허용해 실제 GPU 픽셀을 검증.
- GPU 검사 확대: 0~10,000 nit 7단계, 실제 Tab 캐시와 합성 경로,
  UI-white 80/1,000 nit 변경, 캐시 재사용/갱신 및 12회 표시·숨김,
  패널 외부 영상 보존, 중립 배경의 색 편향 없음, HDR→SDR 전환 시 기존 테마 복원.
- 색차 배치 / transfer / primaries / matrix / range / Force의 76,800개 조합 검사 추가.
  수동 배치가 기존의 독립된 HDR 판정·range 제한을 우회하지 않음을 확인.
- 일반 Release 빌드 성공, 전체 26/26 통과(78.48초), GPU 검사 3.85초.
  HDR 정책·GPU 파이프라인·설정 저장 검사 각각 5회 반복도 모두 통과(23.24초).
- `build-hardware-tonemap-test/LowLatencyCaptureViewer.exe` 647,680바이트.
  이번 배경색 수정 전과 파일 크기 동일. 추가 프레임 연산/복사/큐/라이브러리 없음.
  SHA256: `20E4BDB9ECEECFC6ED0C26E8ABBFF49F4B774282B07D34BD7690789CA4A9F637`.
- 일반 빌드의 프레임 덤프/scRGB/vendor 톤매핑 제어는 모두 OFF.
  위의 기존 테스트 ZIP은 이번 Tab 배경 변경을 포함하지 않으며 덮어쓰지 않았음.
- 실제 캡처 장치나 OBS를 실행하지 않음. ezcap 401에서의 실제 색차 정확성과
  모니터에서의 최종 체감 밝기, 장시간 실기 지연은 여전히 미확인.
  공개 릴리스/태그/버전은 변경하지 않음.
