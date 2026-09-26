# AVerMedia 하드웨어 톤 매핑 초기화 검증

## 현재 상태 — 후속 실장치 결과 반영 (2026-09-16)

**해결 미확인 / 일반 빌드 제외 / 실장치 실험 보류.** 아래 최초 구현·시험 기록은
프로토콜 구현 검증이지 과포화 해결 또는 드라이버 안정성 인증이 아니다.

- `LowLatencyCapture_20260916_033417.log`에서 P010 OFF 명령이 S_OK,
  지원 플래그 0x3으로 수락됐지만 사용자는 과포화가 해결되지 않았다고 보고했다.
- 이후 OBS와 동시 실행을 시도하다 PAGE_FAULT_IN_NONPAGED_AREA가 발생했고,
  Windows 이벤트에 `AVXGC573_x64.sys`가 기록됐다. 새 명령과의 인과관계는
  미확정이다. 동시 실행·실장치 재현·추가 vendor 명령 실험을 자동 수행하지 않는다.
- 일반 구성에서는 vendor 모듈을 앱 타깃의 소스에서 제외하고 호출도 컴파일하지 않는다.
  `LLCV_EXPERIMENTAL_HARDWARE_TONEMAP` 기본값은 OFF이며 F8/scRGB 진단만 켜도
  이 장치 제어는 활성화되지 않는다.
- 명시적으로 ON을 지정하더라도 `LLCV_HDR_FRAME_AUDIT=ON`인 비공개 진단에서만
  빌드를 허용한다. 일반 구성과 조합하면 CMake 및 소스 가드로 거부한다.
- 가짜 COM 단위 테스트에는 모듈을 계속 포함한다. 실제 장치를 열거나 명령을 보내지 않는다.
- 이전에 만든 `GC573-HDR-ToneMapping-Test.zip`은 실험 기능이 켜진 과거 산출물이다.
  이번 분리가 기존 ZIP을 안전한 새 빌드로 바꾸지는 않는다. 재배포하지 않는다.
- 공개 v1.2.8 실행 파일·릴리스·사용자 설정은 변경하지 않는다.

### 분리 후 재검증

- 일반 구성 및 명시적 opt-in 진단 구성 빌드 성공. 실제 앱 실행은 하지 않았다.
- vendor ON + 진단 OFF 구성은 예상대로 CMake에서 거부됨.
- 일반 EXE에는 vendor 제어 로그 문자열이 없고 opt-in 진단 EXE에는 존재함을 확인.
  일반 타깃은 vendor 구현 파일 자체를 컴파일/링크하지 않는다.
- 일반 EXE 643,072바이트로 공개 v1.2.8과 같은 크기. 영상 프레임별 처리 변경 없음.
  전체 지연을 실측한 결과는 아니다.
- 전체 26/26 자동 검사 통과 (77.04초), HDR GPU 검사 포함 (3.45초).
- main.cpp 직접 포함형 테스트의 오래된 오브젝트가 링크되는 증상을 발견하여
  4개 테스트 소스에 명시적인 main.cpp 빌드 의존성을 추가하고 모두 재컴파일했다.
- 공개 v1.2.8 EXE 해시 `8EA3846A5D362DD6E276525C83CED0F974FE3F294307C22AEEADD421E0ACEDCB`
  유지 확인. 기존 배포 ZIP이나 공개 릴리스는 수정하지 않았다.

## 배경과 한계

2026-09-16 GC573 원본 프레임 비교에서 동일한 캡처 형식·메타데이터·HDR 출력 설정인데도
앱 변환 전 P010 값이 달라졌다. OBS 사용 이후 정상화됐다는 사용자 관찰과,
OBS의 하드웨어 톤 매핑 제어는 서로 부합한다. 단, 당시 장치 상태값이 없으므로
하드웨어 톤 매핑이 실제 원인이었다고 확정하지 않는다.

참고한 프로토콜/호출 시점:

- [OBS ConnectFilters](https://github.com/obsproject/libdshowcapture/blob/8878638324393815512f802640b0d5ce940161f1/source/device.cpp#L777-L781)
- [AVerMedia 드라이버 속성](https://github.com/obsproject/libdshowcapture/blob/8878638324393815512f802640b0d5ce940161f1/source/device-vendor.cpp#L50-L79)
- [Microsoft QuerySupported](https://learn.microsoft.com/en-us/windows/win32/directshow/ikspropertyset-querysupported)

## 적용 범위

- 선택된 비디오 필터 이름에 AVerMedia가 포함되고 전용 속성의 SET 지원이 확인된 경우만 쓴다.
- 전용 GUID `8A80D56F-FAC5-4692-A416-CF20D4A18F47`, 속성 ID 2.
- x64 네이티브 KSPROPERTY 구조체 정렬을 유지한다. 데이터 32바이트, instance 8바이트,
  enable DWORD 뒤 4바이트 패딩 및 KSPROPERTY 헤더는 0으로 초기화한다.
- 형식 협상 후, 비디오 핀 연결/그래프 실행 전에 한 번 요청한다.
- P010: HDR→SDR 변환 OFF. Force HDR10 체크와 무관하게 원본 전달을 요청한다.
  P010 자체가 HDR임을 보장하지는 않으며, 기존 HDR 입력 판정은 그대로 유지한다.
- NV12/YUY2/MJPEG: 하드웨어 HDR→SDR 변환 ON.
- Auto/알 수 없는 형식, 다른 제조사, 오디오 전용 그래프는 제어하지 않는다.
- GC553 USB 확장 유닛/Elgato 제어는 이번 범위에 포함하지 않는다.
- QuerySupported 실패/SET 미지원이면 쓰기를 생략한다. OBS처럼 무조건 SET을 시도하지 않는다.
  드라이버가 capability 조회를 구현하지 않았다면 이번 제어는 적용되지 않으며 로그에 남는다.
- SET 실패는 기존 캡처 경로를 막지 않는다. 성공은 명령 수락일 뿐 실제 신호 검증이 아니다.
- 종료 시 이전값을 임의로 복원하지 않는다. 다른 앱 설정을 덮어쓰거나 타이밍 경합을
  만들지 않도록 다음 캡처 시작 시 원하는 형식을 다시 설정한다. 실행 중인 다른 앱이
  장치 상태를 바꾸는 경우까지 보장하지 않는다.

## 자동 검증

`HardwareToneMappingTests`는 실제 장치를 열지 않는 가짜 COM 인터페이스 테스트다.

- 정확한 GUID/ID/버퍼 크기/enable 위치/패딩 확인.
- P010→NV12→P010→YUY2→MJPEG→P010 요청과 재시작 시 재적용.
- 다른 제조사/Auto/잘못된 형식에 QI/GET/SET 호출 없음.
- 인터페이스 없음, null 필터, 지원 안 함, GET 전용, 조회 권한 오류, SET 실패/S_FALSE.
- 성공/생략/실패 로그 구분 및 COM 참조 해제.
- 10,000회 교차 호출: 호출당 최대 SET 1회, 무한 재시도/장치 상태 캐시 없음.

GPU HDR 변환·오디오·설정·창 동작 테스트는 기존 테스트와 함께 실행한다.
이 가상 검증은 실제 GC573 드라이버의 명령 수락이나 과포화 재현/해소를 대신하지 않는다.

## 성능/배포

기존 영상 변환 셰이더/VideoProcessor/스왑체인/오디오 콜백을 수정하지 않는다.
추가 제어는 그래프 시작 시 QI/QuerySupported/SET과 로그뿐이다.
프레임별 호출, 프레임 큐, 복사, 새 DLL/라이브러리 의존성은 추가하지 않는다.
드라이버 처리로 인한 초기화 시간 변화 및 실장치 효과는 별도 측정이 필요하다.

최초 검증 구성은 `build-hardware-tonemap-test`, 비공개 F8 진단 구성은 `build-hw-hdr-audit`.
진단 구성은 원래 HDR10 렌더러이며 scRGB 실험 경로를 활성화하지 않는다.
공개 v1.2.8 실행 파일·릴리스는 변경하지 않는다.

## 실행 결과 (2026-09-16)

- 전체 자동 테스트 26/26 통과, 131.66초. HDR GPU 테스트 포함.
- capability 조회의 S_FALSE도 쓰기 금지로 처리하도록 보강 후 새 단위 테스트 재통과.
- 일반 빌드 645,120바이트: 공개 v1.2.8의 643,072바이트 대비 +2,048바이트.
- DLL 의존성 목록 변화 없음. 공개 v1.2.8 EXE SHA256 유지:
  `8EA3846A5D362DD6E276525C83CED0F974FE3F294307C22AEEADD421E0ACEDCB`.
- 비공개 원래 HDR10/F8 테스트 EXE 655,872바이트, SHA256:
  `B9E6E0E1C156E2FEA5169858CB1920BCA38638B9B3B7C604AF53DC7202C9EA16`.
- `GC573-HDR-ToneMapping-Test.zip` SHA256:
  `3069F6CC810F2B220631C68B2AF85A17A8102623934B4CF293193BFB59FF3094`.
  ZIP 내부 EXE와 빌드 파일 해시 일치 확인.
- 실제 캡처 장치 실행/하드웨어 상태 변경은 자동 검증에서 수행하지 않음.
