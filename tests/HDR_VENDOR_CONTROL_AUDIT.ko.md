# OBS 방식의 캡처 시작 시 톤매핑 요청 및 P010 HDR 전용화

2026-09-27, v1.2.12 기반 로컬 수정본. 릴리스/업로드 없음.

## 근거 및 적용 범위

- [OBS 호출 시점](https://github.com/obsproject/libdshowcapture/blob/c13d4b7b0c66979396ba0a9060c9aafc15bb7b22/source/device.cpp): 형식 설정 이후, 핀 연결/그래프 실행 이전.
- [OBS 제조사별 처리](https://github.com/obsproject/libdshowcapture/blob/c13d4b7b0c66979396ba0a9060c9aafc15bb7b22/source/device-vendor.cpp): P010은 내부 HDR→SDR 톤매핑 OFF, 다른 캡처 형식은 ON 요청.
- [Elgato 공식 KS 프로토콜](https://github.com/elgatosf/capture-device-support/blob/fe9630974d47f51bf54826e72fb8b654e620aa93/SampleCode/DriverInterface.cpp).
- [Elgato 공식 USB 프로토콜](https://github.com/elgatosf/capture-device-support/blob/fe9630974d47f51bf54826e72fb8b654e620aa93/Library/ElgatoUVCDevice.cpp) 및 [Windows HID 전송](https://github.com/elgatosf/capture-device-support/blob/fe9630974d47f51bf54826e72fb8b654e620aa93/Library/win/EGAVHIDImplementation.cpp).
- [Windows 물리 장치 ContainerId](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/devpkey-device-containerid).

확인한 OBS DirectShow 소스의 제조사 톤매핑 제어 계열은 AVerMedia와 Elgato다.
공개 프로토콜이 확인되지 않은 제조사나 모델에 명령을 추측해 보내지 않는다.
동일 정책을 적용하되 모델 이름이나 P010 지원만으로 명령 지원을 단정하지 않는다.

### 일반 빌드에 포함한 경로

| 경로 | 지원 확인 및 쓰기 조건 |
| --- | --- |
| AVerMedia KS | 선택된 필터가 공식 속성 GUID/ID 2의 SET 지원을 보고할 때만 요청 |
| Elgato KS | 선택된 필터가 공식 속성 GUID/ID 722의 SET 지원을 보고할 때만 요청 |
| AVerMedia UVC | 선택된 필터의 topology에서 공식 확장 GUID/ID 11을 읽을 수 있는 유일한 노드를 확인한 뒤 요청 |
| Elgato USB/HID | 선택된 비디오 장치와 VID/PID 및 물리 ContainerId가 일치하는 유일한 HID 인터페이스에 요청 |

Elgato KS 공식 예제는 4K60 Pro MK.2를 명시한다. 다른 모델은 실제 지원 조회로 판정한다.
USB/HID는 공개 참조에 있는 VID 0FD9, PID 006A(HD60 S+), 0082(HD60 X),
008A(HD60 X Rev2)만 허용한다. 이 목록을 근거로 4K X 등 다른 USB 모델까지
지원한다고 주장하지 않는다.

OBS의 광범위한 USB 쓰기 fallback을 그대로 복사하지 않았다.
실제 선택에 성공한 비디오 moniker에서 DevicePath를 읽고, 해당 물리 장치만 식별한다.
알 수 없는 VID/PID, 식별 실패, 중복 후보, 잘못된 보고서 크기에서는 쓰지 않는다.
HID 전송 직전에도 장치 identity와 보고서 크기를 다시 확인한다.

## AVerMedia 과거 실험과 현재 결정

`HARDWARE_TONE_MAPPING_AUDIT.ko.md`는 2026-09-16 당시 기록으로 보존한다.
그 기록 및 이전 상태의 실험용 빌드 제한은 이번 사용자의 AVerMedia 적용 요청으로 변경됐다.
현재는 AVerMedia KS/UVC와 Elgato KS/검증된 USB 경로를 일반 빌드에 포함한다.
기존 `LLCV_EXPERIMENTAL_HARDWARE_TONEMAP` 빌드 제한은 제거했다.

과거 GC573 실험에서는 명령 수락 후에도 증상이 해결되지 않았으며,
이후 OBS 동시 실행 시 `AVXGC573_x64.sys` 관련 블루스크린이 기록됐다.
톤매핑 명령과의 인과관계는 미확정이다. 이번에는 실제 카드나 OBS를 실행하지 않았으며,
실제 드라이버 안전성 또는 그 증상 해결을 확인했다고 주장하지 않는다.

## 안전 및 성능 범위

- 협상 완료된 형식에 적용: P010 OFF, NV12/YUY2/MJPEG ON. Auto/잘못된 값은 무동작.
- 시작 시에만 요청하며 한 번의 초기화에서 SET은 최대 1회. 재시도/주기적 제어 없음.
- 제조사 이름은 확인 순서에만 사용. 쓰기는 실제 인터페이스/속성/장치 식별로 결정.
- 미지원 경로만 다음 프로토콜 확인으로 진행. 예상 밖의 조회 오류, 모호한 식별,
  SET 성공 또는 실패 이후에는 다른 경로로 다시 쓰지 않음.
- UVC는 최대 64개 노드, 올바른 길이의 응답만 허용하고 필요한 두 바이트 외에는 보존.
- HID는 최대 4,096개 인터페이스 확인, 보고서는 최대 4,096바이트이며 남는 부분은 0.
- 제어 실패는 로그만 남기며 캡처 그래프 연결 자체를 실패 처리하지 않음.
- 명령 수락은 실제 입력이 PQ/BT.2020으로 인코딩됐다는 증거가 아님.
- 종료 복원 명령 없음. 다음 SDR 시작은 ON 요청. 다른 앱과 같은 장치 동시 사용은 피함.
- 영상·오디오 프레임 루프, 큐, 셰이더, GPU 출력 형식, 복사 횟수, Present 정책 변경 없음.
- HID/SetupAPI는 Windows 시스템 라이브러리. 외부 SDK/DLL/프레임워크를 배포하지 않음.
- 초기화 시 드라이버 요청 때문에 시작 시간이 늘 수 있음. 종단 지연 실측은 아님.

## P010 HDR 전용 정리

- P010 렌더러의 SDR 경로를 제거하고, HDR 입력 정보가 없는 요청은 GPU 생성 전 거부한다.
- 명시적 SDR P010은 NV12/YUY2 사용 안내와 함께 거부한다. SDR 분류 자체는 진단에 유지한다.
- 메타데이터가 없는 P010의 PQ/BT.2020 기본 가정과 사용자 강제 옵션은 유지한다.
- 명시적 SDR/HLG 등을 P010이라는 이유만으로 자동으로 HDR10으로 바꾸지 않는다.
- 기존 색상 범위/색차 배치 검증을 유지한다.
- 설정 도움말, 오류 안내 및 한국어/영어 영상 문서에 HDR 전용 정책을 반영했다.

## 검증 결과

모의 COM/HID 테스트와 합성 GPU 프레임만 사용했다. 실제 캡처 장치에 명령을 보내지 않았다.

- MSVC x64 Release 빌드 성공.
- 전체 CTest **31/31 통과**, 건너뜀 없음, 122.61초.
- HDR/SDR 합성 GPU, WASAPI Shared 재생 모의 검사, 25ms/5.1,
  창·설정·프레임 전달·형식 복구 회귀 검사 포함.
- HDR 색상 패치 729개, 최대 오차 **0.558/1023 코드값**으로 직전 기준과 동일.
- HDR 메타데이터 조합 76,800개 및 SDR P010 조기 거부 후 HDR 재초기화 검사 통과.
- 제조사 KS/UVC/HID의 정확한 GUID/ID/버퍼, 성공·실패 HRESULT, 참조 해제,
  반복 HDR/SDR 전환, 미지원/중복 장치, 응답 길이/보고서 경계 검사 통과.
- 전체 검사 뒤 장치 선택 실패 시 identity 초기화 및 HID 식별/길이 경계 사례를 추가했다.
  이 테스트만 재빌드한 뒤 HardwareToneMappingTests, VendorUsbToneMappingTests,
  DirectShowDevicesTests를 각각 **10회 반복하여 모두 통과**했다.
- 실행 파일 **671,232 → 685,568바이트 (+14,336바이트 / 14KiB)**.
  직전 Elgato-only 수정본 대비이며 공개 v1.2.12(668,672바이트) 대비 총 +16,896바이트.
- 새 DLL 의존성은 Windows 기본 HID.DLL/SETUPAPI.dll이며 외부 배포 파일은 추가되지 않음.
- 기존 MonitorMoveTests의 `monitors` 이름 가림 경고 외 새 모듈 컴파일 경고/오류 없음.
- 실제 카드의 명령 수락, HDR 밝기/색감 개선, 드라이버 안정성, 장시간 사용,
  종단 지연은 이번 검증 범위 밖이다. 제보 해결을 확정하지 않는다.

수정 EXE SHA256:
`18C14248F0CD821FA12565CCC15D01DAFDD9B9F1F27146BA5F94FADFAC040D1A`
