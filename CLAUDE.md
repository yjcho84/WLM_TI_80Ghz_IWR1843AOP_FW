# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 프로젝트 개요

TI Out-of-Box 데모를 기반으로 한 IWR1843AOP 레이더 펌웨어이며, 수위계 제품에 쓰인다. TI 칩은 SPI1 슬레이브로 nRF52(BLE 게이트웨이)에 연결된다. 파일별 역할은 `code_description.txt`에 설명되어 있다.

- `Out_Of_Box_1843_MSS`: R4F. 시스템 제어, CLI, UART 출력, SPI1 슬레이브
- `out_of_box_1843_dss`: C674x DSP. 신호처리 체인

## 빌드와 테스트

**빌드는 사용자가 직접 한다. 빌드나 컴파일을 실행하거나 제안하지 말고, 코드 수정과 변경 요약까지만 한다.**

- 테스트와 린트는 없다. 동작 검증은 사용자가 실제 하드웨어에서 UART 로그와 nRF RTT 로그로 한다.
- IDE는 CCS Theia이고, 실사용 빌드 구성은 `aop`다. `aop`에만 `XWR18XX_AOP_ANTENNA_PATTERN`이 정의되어 있다.
- MSS 빌드 후 단계가 MSS 이미지, radarss 펌웨어, DSS `.bin`을 하나로 묶어 `Out_Of_Box_1843_MSS/aop/out_of_box_1843_aop.bin`을 만든다. 따라서 DSS를 수정하면 DSS와 MSS를 모두 다시 빌드해야 한다.
- 빌드는 `C:/ti/mmwave_sdk_03_05_00_04`를 사용한다. `.ccsproject`에 적힌 03.06은 템플릿 값이다. SDK 드라이버 내부를 확인할 때는 이 경로의 `packages/`를 본다.

## 저장소 주의사항

- 빌드 산출물(`aop/`, `isk/`, `.clangd` 인덱스)이 git에 추적되고 있어 빌드할 때마다 수정 파일로 표시된다. 소스 변경을 확인할 때는 `*.c`, `*.h`, `*.cfg`, `*.cmd`로 범위를 좁힌다.
- `mmw_cli.c`에는 `gMmwDemoAutoCfgCommands[]`가 두 개 있다. 앞쪽은 주석 처리된 옛 버전이고, 뒤쪽이 실제로 쓰인다.
- `gMmwDemoAutoCfgCommands_설명.txt`는 파라미터 설명용으로 쓰고, 그 안의 행 번호는 믿지 않는다.
- 기존 코드는 수정 전 줄을 주석으로 남기고 `//->26.10.23 ...`처럼 날짜를 표시한다. 이 방식을 따른다.

## 커밋 규칙

사용자가 아래처럼 말하면 해당 저장소의 변경 사항을 모두 스테이징(`git add -A`)한 뒤 커밋한다. 빌드 산출물도 함께 커밋하는 것이 기존 방식이다.

| 사용자 지시 | 저장소 경로 | GitHub 저장소 |
|---|---|---|
| "TI 커밋해" | `C:\Users\sandr\workspace_ccstheia` | `WLM_TI_80Ghz_IWR1843AOP_FW` (remote `origin`) |
| "nRF52 커밋해" | `C:\ncs\v2.6.0\samples\SUINTECH_WaterLevelMeter` | `SUINRADAR_PUBLIC` (remote `SUINRadarLevelMeter`) |

- 커밋 메시지는 커밋하는 시점의 날짜와 시간만 `YYMMDD_HH:MM` 형식으로 쓴다. 예: 26년 11월 19일 18시 11분 → `261119_18:11`. 시간은 커밋 직전에 `date`로 확인한다.
- 이 규칙의 커밋 메시지에는 Co-Authored-By 등 다른 줄을 붙이지 않는다.
- 커밋 후 위 표의 remote로 현재 브랜치를 push까지 완료한다(TI: `git push origin master`, nRF52: `git push SUINRadarLevelMeter main`). push가 실패하면 강제 push(`--force`)를 하지 말고 오류 내용을 사용자에게 알린다.
- 완료 후 커밋 해시, 포함된 파일 수, push 결과를 알린다.

## 관련 코드 위치

SPI 링크를 수정할 때는 TI와 nRF 양쪽을 함께 고친다. nRF52 코드(Zephyr/NCS v2.6.0)도 빌드하지 않는다. nRF 코드는 별도 git 저장소(`C:\ncs\v2.6.0\samples\SUINTECH_WaterLevelMeter`)다.

- 샘플: `samples\5_80GD_TI_V1` (`src/main.c`만 링크되고 `src/test_main.c`는 링크되지 않는다)
- 공유 소스: `source\`
  - `cho_test_code_260807.c`: SPI 마스터, TI 패킷 파서, 거리 보정, Modbus 응답
  - `cho_ble_protocol.c`: BLE 명령 파서
  - `cho_fds.c`: 설정값 저장
- 헤더: `include\` (FDS ID와 기본값은 `cho_fds.h`)
- 보드 정의: `C:\CHOYJ_\my_boards\boards\arm\ti_iwr1843_nrf52_v1\`
- 프로토콜 사양서(App, nRF, TI 공통): `C:\CHOYJ_\ClaudeAI_CHOYJ\test_sample1_261016.xlsx`. 사용자가 여기에 명령을 추가한 뒤 구현을 요청한다.

공유 소스는 다른 제품도 사용하므로 수정 범위에 주의한다.

- `cho_ble_protocol.c`와 `cho_fds.c`는 샘플 1, 3(60GD)도 링크한다. `cho_test_code_260807.c`는 샘플 5만 링크하므로, `cho_ble_protocol.c`에서 호출하는 새 TI 함수에는 `__weak` 스텁이 필요하다.
- `cho_ble_protocol_current.c`와 `cho_fds_current.c`는 샘플 2, 4(60GA) 전용이다. 이름과 달리 TI 제품에서는 쓰이지 않으므로 수정하지 않는다.

## 아키텍처

### 데이터 흐름

명령: App → BLE `spiA...` → nRF(FDS 저장) → SPI `spiM...` → `mmwdemo_spi1.c` → `mmw_cli.c`

측정 결과: BSS(RF) → DSS(`objectdetection.c`의 Range, Doppler, CFAR, AoA) → HSRAM → MSS → 로깅 UART TLV → nRF UART1(`uartOneD_parse_packet`) → 거리 선택과 보정 → BLE, RS485 Modbus. 현재 SPI1로는 측정 데이터를 보내지 않고 4바이트 응답 코드만 보낸다.

nRF의 BLE 파싱, SPI 송신, TI 패킷 처리는 모두 `main_thread_ti()` 메인 루프에서 실행되어 SPI 접근이 직렬화된다. `ti_boot_cfg_send()`가 끝나기 전에는 메인 루프가 시작되지 않으므로 BLE 명령도 처리되지 않는다(최대 6초).

### 부팅 순서 (`mss_main.c`의 `MmwDemo_initTask`)

1. CLI를 초기화한다. 이 단계에서는 auto-config를 실행하지 않는다.
2. SPI1을 초기화하고, `initTask` 우선순위를 SPI1 태스크보다 높인 뒤 SPI1 태스크를 생성한다.
3. `MmwDemo_spi1WaitBootCfg()`로 nRF의 부팅 설정을 최대 5초 기다린다. `spiMaa`와 `spiMba`를 모두 받으면 대기가 끝나고, 시간이 초과되면 테이블 기본값을 사용한다.
4. `MmwDemo_CLIRunAutoConfig()`가 `sensorStart`까지 실행한다.

nRF 쪽(`main()`의 `ti_boot_cfg_send()`)은 다음 순서로 보낸다. 각 명령은 코드 3을 받을 때까지 100ms 간격으로 재시도하며, 전체 제한 시간은 명령별이 아니라 합계 6초다.

1. FDS에 값이 있는 `spiMd`~`spiMi`만 보낸다. 값이 없으면 TI 기본값을 유지한다.
2. `spiMaa####`를 보낸다. FDS에 값이 없으면 기본값 1000을 보낸다.
3. `spiMba#`을 보낸다. FDS에 값이 없으면 기본값 0을 보낸다. TI auto-config를 시작시키는 명령이므로 항상 마지막에 둔다.

**`spiMca`(MBF)는 이 순서에 없다.** nRF가 TI 패킷을 처음 받을 때, 또는 3초 이상 끊겼다가 다시 받을 때 `ti_mbf_sync_process()`가 FDS 값을 보낸다. 따라서 MBF 저장값은 센서 시작 후에 적용된다.

### 런타임 설정 경로

자동 설정 테이블의 일부 항목은 변경 가능한 RAM 버퍼(`gMmwDemo*Cmd`)를 가리키며, 재부팅하면 기본값으로 돌아간다.

| 종류 | BLE → SPI | TI CLI 명령 | FDS ID | BLE 수신 시 nRF 동작 | TI 적용 시점 |
|---|---|---|---|---|---|
| 부팅 전용 | `spiAa` → `spiMaa` | `frameCfg` | 33 | FDS 저장만 | 다음 전원 인가 후 첫 `sensorStart` |
| 부팅 전용 | `spiAb` → `spiMba` | `lowPower` | 34 | FDS 저장만 | 다음 전원 인가 후 첫 `sensorStart` |
| 동적 | `spiAc` → `spiMca` | `multiObjBeamForming` | 31, 32 | FDS 저장 후 SPI 송신 | 다음 프레임 |
| 동적 | `spiAd`~`spiAf` → `spiMd`~`spiMf` | `clutterRemoval`, `calibDcRangeSig`, `extendedMaxVelocity` | 35~37 | FDS 저장 후 SPI 송신 | 다음 프레임 |
| 동적 | `spiAg` → `spiMg` | `aoaFovCfg` | 38 | FDS 저장 후 SPI 송신 | 다음 프레임 |
| 동적 | `spiAh`, `spiAi` → `spiMh`, `spiMi` | `cfarFovCfg` (거리, 속도) | 39, 40 | FDS 저장 후 SPI 송신 | 다음 프레임 |

- `spiA?L`은 FDS 값을 읽어 BLE로 응답한다. `spiAa`, `spiAb`, `spiAd`~`spiAi`는 FDS에 값이 없으면 기본값으로 응답한다.
- 쓰기 명령은 BLE 응답이 없다(사양서와 일치). SPI 전송이 실패해도 nRF 로그만 남고 재시도하지 않으므로, App은 실패를 알 수 없다. 저장된 값은 다음 부팅 때 복원된다.
- auto-config 시작 후 받은 `spiMaa`, `spiMba`도 코드 3으로 응답하고 값은 무시한다(TI 내부 상태 1). 코드 3이 곧 적용 완료를 뜻하지는 않는다.
- FDS ID 38~40은 압축 형식으로 저장한다. 형식은 `cho_fds.h`의 주석을 따른다. MBF 임계값(ID 32)은 float 비트 값으로 저장한다.

### SPI1 프로토콜 (`mmwdemo_spi1.c`)

- 물리 설정: 125kHz(보드 DTS의 `spi-max-frequency`), SPI 모드 0, MSB 우선, 8비트. CS는 nRF P0.02다. CPHA1은 시험 결과 더 나빠져 되돌렸다.
- 명령은 고정 길이 ASCII이고, CS 한 번에 1바이트씩 전송한다. 최대 길이는 `MMWDEMO_SPI1_CMD_MAX_LEN`과 nRF `SPI_CMD_MAX_LEN`(둘 다 17)이다. DMA를 쓰지 않는 경로에서는 CS당 2바이트를 받지 못하므로 `MMWDEMO_SPI1_CHUNK_LEN`은 1로 유지한다.
- `spiM...` 명령은 하나의 매처가 `gMmwDemoSpi1CmdPatterns[]`를 사용해 처리한다. 5번째 바이트로 패턴을 고르므로 이 바이트는 명령마다 달라야 한다. `#`은 숫자 자리다. 현재 `a`~`i`를 사용 중이다.
- nRF는 명령 전송 후 50ms 바쁜 대기(`SPI_CHUNK_GAP_US`)를 하고 4바이트 응답을 읽는다. TI 핸들러는 이 시간 안에 값 처리를 끝내고 응답 전송에 들어가야 한다. 이 대기 중 쌓이는 TI 패킷 때문에 nRF의 UART1 링 버퍼는 4096바이트로 잡혀 있다.
- 응답은 4바이트 온도계 코드다. 코드 N은 앞쪽 `0xFF` N개와 나머지 `0x00`으로 보내고, 수신 측은 앞쪽의 정확한 `0xFF` 개수를 센다. 현재 코드 3(`FF FF FF 00`)은 수락, 코드 0(`00 00 00 00`)은 거부이며 다른 코드는 쓰지 않는다.

### 사양서와 코드의 차이

사양서를 읽을 때는 셀 메모도 함께 읽는다. 고정 길이 SPI 형식은 G28(`spiMaa`), F33(`spiMga`), F34(`spiMha`)의 메모에 적혀 있다. 코드에서는 `gMmwDemoSpi1CmdPatterns[]`와 `ti_spi_format_dyn_cmd()`가 기준이다.

| 항목 | 사양서 | 코드 |
|---|---|---|
| `spiMh` 셀 예시(B34, E34) | `spiMha1.25` (F34 메모는 `spiMha01.25`) | 5글자 고정 `spiMha01.25` |
| `spiAhL` 응답 예시(C19, D19) | `spiAha15.0` | 소수 둘째 자리까지 `spiAha15.00` |
| `spiAeL` 행의 nRF 열(D13, E13) | `spiAda1`, `spiAdL` | 오타이며 실제는 `spiAea1`, `spiAeL` |
| MBF 임계값 범위 | 0.25~1.00 | nRF, TI 모두 강제하지 않음. SPI 형식상 최대 9.99 |

아직 고치지 않은 `spiAca` 처리 문제가 있다. 사용자에게 이미 알린 내용이다.

- 9.99 초과 값 검사 전에 FDS에 먼저 저장한다.
- BLE 경로는 임계값을 소수 셋째 자리에서 버리고, `ti_mbf_sync_process()`는 반올림한다. 예를 들어 `0.555`는 즉시 전송 때 `0.55`, 재동기화 때 `0.56`이 될 수 있다.

### 새 SPI 명령 추가

1. TI: `gMmwDemoSpi1CmdPatterns[]`와 `MMWDEMO_SPI1_CMD_*`에 패턴을 추가하고, `MmwDemo_spi1SlaveTask`의 핸들러 분기를 연결한다. 필요하면 `MMWDEMO_SPI1_CMD_MAX_LEN`도 늘린다.
2. TI: `mmw_cli.c`에 변경 가능한 테이블 버퍼와 `MmwDemo_CLIUpdate*()`를 추가한다.
3. nRF `cho_fds.h`: FDS ID와 기본값을 추가한다. 기본값은 TI 테이블의 초기값과 같아야 한다. 현재 사용 범위는 31~40이다.
4. nRF `cho_ble_protocol.c`: `ble_data_parser()`의 `case`와 `ble_spi_dyn_params[]`, `ble_spi_dyn_param()`을 수정한다.
5. nRF `cho_test_code_260807.c`: `ti_dyn_params[]`와 `ti_spi_format_dyn_cmd()`를 수정한다. 여기에 등록하면 `ti_boot_cfg_send()`가 부팅 때 자동으로 복원한다. `SPI_CMD_MAX_LEN`은 TI와 맞춘다.

## 하드웨어와 RTOS 제약

- **MISO 하강 전이 결함:** MISO가 1에서 0으로 바뀌는 비트가 패턴에 따라 재현성 있게 1로 읽힌다(반대 방향 오류는 관측되지 않음). 원인은 하드웨어로 추정되지만 수정되지 않았으므로, 임의 데이터를 MISO로 보내지 않고 온도계 코드로 우회한다. 원시 값 `FF FF FF 80`은 코드 3으로 정상 해석된다. 숫자 응답이 필요하면 새 인코딩을 설계해야 한다.
- **SPI1 태스크는 유휴 상태에서도 양보하지 않는다:** `dmaEnable=0`이면 SDK 전송 함수가 마스터 클럭을 바쁜 대기하므로, 우선순위 2 이하의 태스크가 실행되지 못한다. SPI1 태스크 생성 후에도 실행되어야 하는 코드는 우선순위를 더 높게 둔다.
- **SPI1에서 DMA를 켜지 않는다:** 빌드에 `DebugP_ASSERT_ENABLED`가 켜져 있어 SDK DMA ISR의 `DebugP_assert(0)`이 R4F 전체를 멈출 수 있다.
- **SPI 수신 경로에서 `CLI_write`를 호출하지 않는다:** 폴링 방식 UART 송신이라 느려서, 처리 중에 들어오는 명령 바이트를 놓친다. 응답을 먼저 보내고 로그는 그 뒤에 남긴다. 예외는 매처의 resync 경고뿐이다. 응답 후 로그를 쓰는 동안에도 다음 명령을 놓칠 수 있는데, 코드상 부팅 명령은 재시도로 복구되지만 런타임 명령은 재시도가 없다(코드 분석 결과이며 하드웨어로 확인한 내용은 아니다).
- **nRF 주석 일부는 옛 분석이다:** `cho_test_code_260807.c`의 `SPI_CS_SETUP_DELAY_US` 설명(첫 클럭 비트 손상)과 `SPI_CHUNK_GAP_US` 설명(2바이트 청크)은 원인 규명 전의 내용이다. 200us 지연은 시험에서 결함에 영향이 없었지만 코드에는 남아 있다.
- **`MmwDemo_initTask`의 블로킹 호출을 조심한다:** DPM이나 mmWave 제어 태스크가 post하는 세마포어를 기다리다 멈추면 오류 출력 없이 이후 초기화가 모두 실행되지 않는다. 새 초기화 코드를 UART 초기화 전에 넣어도 실패가 보이지 않는다.
- **`strtok` 충돌을 피한다:** auto-config가 `strtok`를 사용하므로 SPI 경로에서는 argv를 직접 구성한다(`MmwDemo_CLIApplyDynCfg` 참고).
- **TI 단독 리셋은 고려하지 않는다:** 사용자 하드웨어에서는 발생하지 않는다고 확인되었으므로 다시 제기하지 않는다. 다만 `ti_mbf_sync_process()`는 정상 부팅 때 MBF를 복원하는 유일한 경로이므로 제거하면 안 된다.

## 포인트 클라우드와 거리 선택

UART TLV의 포인트 순서는 도플러 빈 오름차순, 그 안에서 레인지 오름차순이다. 전체가 거리순은 아니다. SNR은 side-info TLV(타입 7, 0.1dB 단위)에 같은 인덱스로 실려 온다. TI 펌웨어는 포인트를 정렬하지 않으며, 선택은 nRF가 한다.

nRF `uartOneD_parse_packet()`의 동작은 다음과 같다.

- 0.30m(`TI_MIN_RANGE_M`) 이상인 점 중 SNR이 가장 큰 점의 거리를 고른다. 전체를 정렬하지는 않는다.
- side-info TLV가 없으면 0.30m 이상에서 가장 가까운 점을 고른다.
- 유효한 점이 없으면 이전 값을 유지한다.
- `RADAR_LOG_ALL_POINTS`가 현재 1이라 프레임마다 모든 점을 로그로 출력한다.

출력 경로마다 쓰는 값이 다르며, 이 차이는 의도된 동작이므로 통일하지 않는다. 모두 `distance_calibrate_mm()`으로 보정(`측정값 × 0.9986 − 0.0968m`)한다.

- BLE(`send_dist_ble`, 1초 주기): 필터를 거치지 않은 `obj1_distance_mm`
- RS485 Modbus: 헌팅 방지 필터를 거친 `obj1_distance_filtered_mm`
