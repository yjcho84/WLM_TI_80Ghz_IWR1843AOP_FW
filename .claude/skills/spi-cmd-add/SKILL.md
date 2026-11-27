---
name: spi-cmd-add
description: TI와 nRF 양쪽에 새 SPI 런타임 명령(spiM...)과 BLE 명령(spiA...)을 추가하는 절차. 사양서(xlsx)에 새 명령이 추가되어 구현을 요청받았을 때 사용한다.
---

# 새 SPI 명령 추가

CLAUDE.md의 SPI1 프로토콜, 런타임 설정 경로, 하드웨어 제약을 함께 따른다.

1. TI: `gMmwDemoSpi1CmdPatterns[]`와 `MMWDEMO_SPI1_CMD_*`에 패턴을 추가하고, `MmwDemo_spi1SlaveTask`의 핸들러 분기를 연결한다. 필요하면 `MMWDEMO_SPI1_CMD_MAX_LEN`도 늘린다.
2. TI: `mmw_cli.c`에 변경 가능한 테이블 버퍼와 `MmwDemo_CLIUpdate*()`를 추가한다.
3. nRF `cho_fds.h`: FDS ID와 기본값을 추가한다. 기본값은 TI 테이블의 초기값과 같아야 한다. 현재 사용 범위는 31~40이다.
4. nRF `cho_ble_protocol.c`: `ble_data_parser()`의 `case`와 `ble_spi_dyn_params[]`, `ble_spi_dyn_param()`을 수정한다.
5. nRF `cho_test_code_260807.c`: `ti_dyn_params[]`와 `ti_spi_format_dyn_cmd()`를 수정한다. 여기에 등록하면 `ti_boot_cfg_send()`가 부팅 때 자동으로 복원한다. `SPI_CMD_MAX_LEN`은 TI와 맞춘다.

## 사양서와 코드의 차이

사양서를 읽을 때는 셀 메모도 함께 읽는다. 고정 길이 SPI 형식은 G28(`spiMaa`), F33(`spiMga`), F34(`spiMha`)의 메모에 적혀 있다. 코드에서는 `gMmwDemoSpi1CmdPatterns[]`와 `ti_spi_format_dyn_cmd()`가 기준이다.

| 항목 | 사양서 | 코드 |
|---|---|---|
| `spiMh` 셀 예시(B34, E34) | `spiMha1.25` (F34 메모는 `spiMha01.25`) | 5글자 고정 `spiMha01.25` |
| `spiAhL` 응답 예시(C19, D19) | `spiAha15.0` | 소수 둘째 자리까지 `spiAha15.00` |
| `spiAeL` 행의 nRF 열(D13, E13) | `spiAda1`, `spiAdL` | 오타이며 실제는 `spiAea1`, `spiAeL` |
| MBF 임계값 범위 | 0.25~1.00 | nRF, TI 모두 강제하지 않음. SPI 형식상 최대 9.99 |

미해결 `spiAca` 처리 문제는 CLAUDE.md에 있다.
