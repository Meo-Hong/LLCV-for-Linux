# OBS HDR 비교 및 P010 기본 판정 검증

검증일: 2026-09-27. 기반: v1.2.12의 로컬 수정본. 릴리스/업로드 없음.

이 문서는 P010 기본 판정 변경 시점의 기록이다. 이후 같은 날 추가한
선택 필터 Elgato 톤매핑 제어와 유지한 안전 제한은
[후속 검증](HDR_VENDOR_CONTROL_AUDIT.ko.md)을 참고한다.

## 비교 기준

OBS Studio `50530ce9046599e698c5d2068e4f053fae2318f6`,
libdshowcapture `c13d4b7b0c66979396ba0a9060c9aafc15bb7b22`의 공식 소스를 읽었다.
OBS 실행 결과와 동일 프레임을 실기기로 대조한 검증은 아니다.

- [DirectShow 입력 판정](https://github.com/obsproject/obs-studio/blob/50530ce9046599e698c5d2068e4f053fae2318f6/plugins/win-dshow/win-dshow.cpp): `GetColorSpace`, `Activate`, `SetupBuffering`
- [P010 셰이더](https://github.com/obsproject/obs-studio/blob/50530ce9046599e698c5d2068e4f053fae2318f6/libobs/data/format_conversion.effect): `PSP010_PQ_2020_709_Reverse`
- [출력 색 공간](https://github.com/obsproject/obs-studio/blob/50530ce9046599e698c5d2068e4f053fae2318f6/libobs-d3d11/d3d11-subsystem.cpp): 모니터 HDR 상태에 따른 FP16/scRGB 선택
- [소스 합성](https://github.com/obsproject/obs-studio/blob/50530ce9046599e698c5d2068e4f053fae2318f6/libobs/obs-source.c): 출력 색 공간에 따른 `DrawTonemap`/광량 단위 변환
- [장치 톤매핑 호출](https://github.com/obsproject/libdshowcapture/blob/c13d4b7b0c66979396ba0a9060c9aafc15bb7b22/source/device.cpp), [제조사별 구현](https://github.com/obsproject/libdshowcapture/blob/c13d4b7b0c66979396ba0a9060c9aafc15bb7b22/source/device-vendor.cpp)

## 관련 차이와 이번 범위

| 구분 | OBS | 이 앱 / 이번 처리 |
| --- | --- | --- |
| P010 기본 색 해석 | 색 공간 기본값에서 PQ 선택 | 전달 함수·색역 정보가 모두 없을 때 PQ/BT.2020 가정으로 변경 |
| 명시적인 색 정보 | 소스의 사용자 색 공간 선택 제공 | 기존 메타데이터 검증 유지. SDR/HLG/불완전한 BT.2020 정보를 자동 PQ로 덮어쓰지 않음 |
| 색 변환 | GPU 셰이더, PQ 선형화와 색역 변환 | 기존 D3D11 Video Processor의 P010→RGB10/PQ 유지, GPU 출력 재검증 |
| 표시 모니터 | HDR 화면에서 FP16/scRGB, SDR 대상에 톤매핑 경로 | PQ HDR10 직접 출력 유지. SDR 모니터용 자체 톤매핑은 없음 |
| 입력 지원 범위 | PQ/HLG, Full/Limited 선택 등 | PQ/BT.2020/Limited 및 Left/Top-left 검증 유지. 지원 범위 확장 없음 |
| 장치 내부 HDR→SDR | 지원 장치에 제조사별 명령, P010에서는 톤매핑 끔 요청 | 정식 경로에 새 장치 명령 없음. 기존 실험용 AVerMedia 제어도 활성화하지 않음 |
| 대기/합성 | 비동기 소스 버퍼링 정책과 장면 합성 | 최신 프레임 전달·업로드·Present 루프 변경 없음 |

P010은 저장 형식이므로 새 기본값도 **HDR 감지 성공이 아닌 가정**이다.
색 정보 없는 SDR P010도 HDR로 해석될 수 있다. 이 경우 NV12/YUY2를 사용한다.
Force HDR10을 끄는 것은 새 자동 가정을 끄는 동작이 아니다. 한국어/영어
도움말과 영상 문서에 이 점을 명시했다. 새 설정 키나 기존 설정 마이그레이션은 없다.

기본 가정에도 범위/색차 검증은 적용된다. Full-range나 지원하지 않는 색차 배치는
자동으로 무시하지 않는다. 명시적인 Force HDR10의 기존 의미는 유지했다.
실제 SDR로 바뀐 연결 메타데이터는 오래된 HDR 튜플을 버리는 기존 정책을 유지한다.

OBS의 제조사별 제어 코드가 있다는 사실만으로 **4K Pro가 해당 명령을 지원하거나
이번 제보의 원인이 카드 톤매핑이라고 확정할 수 없다**. 무차별 HID 장치 탐색/쓰기나
미확인 드라이버 명령을 이식하지 않았다. 이는 별도 모델·드라이버 검증이 필요하다.

## 검증 결과

- MSVC x64 Release 빌드 성공. 전체 CTest **30/30 통과**, 119.02초.
- HDR 판정 교차 조합 **76,800개** 및 범위/색차/반복 재협상 회귀 통과.
- 실제 RTX 3080 GPU에서 합성 프레임 출력·읽기 검증. 캡처 장치/OBS를 열지 않음.
- HDR 729색 패치 최대 오차 **0.558 / 1023 코드**, 수정 전 기준과 동일.
- 1,024단계 명도 램프, 0.1~10,000nit 기준 패치, stride/업로드 링,
  HDR OSD, SDR 전환, 색차 해석 및 4K NV12/YUY2 회귀 통과.
- 새 기본 판정 → 실제 HDR 렌더러 연결 확인. 자동/강제 HDR의 동일 PQ 백색
  GPU 출력값 일치. 명시적 SDR → 정보 없는 P010 재초기화도 통과.
- GPU 테스트 건너뜀 없음. 표시 모니터는 SDR 상태였으므로 최종 HDR 패널의
  광량/톤매핑 정확도 또는 제보자의 증상 해결을 검증한 것은 아니다.
- 실행 파일 **668,672 → 669,184바이트 (+512바이트)**. 새 런타임 의존성 없음.
- 변경은 초기화 시 판정 및 안내/테스트에 국한된다. 프레임 큐, GPU 패스,
  CPU 프레임 복사, 텍스처 형식, Present 정책은 추가/변경하지 않았다.
- VP의 GPU 타임스탬프 측정은 이 환경에서 유효하지 않아 속도 우위나
  지연 0ms를 주장하지 않는다. 실제 종단 지연·장시간 캡처는 별도 검증 사항이다.

수정 실행 파일 SHA256:
`D78916835AB4F1F92C768737E5A8929BEA0B9438DAF3FE83F605844913D9708C`

## 남은 확인

제보자의 로그에서 실제 HDR 판정, 표시 모니터 HDR 상태, 캡처 원본을 구분해야 한다.
입력 자체가 톤매핑된 SDR이거나 실제 표시 모니터가 SDR인 경우에는 이번 기본
판정 변경만으로 해결되지 않는다. OBS와 출력 경로가 다르다는 이유만으로 scRGB
전체 교체를 확정하지 않으며, 장치/표시 경로를 포함한 후속 비교가 필요하다.
