---
name: radar-log-analysis
description: 수위계 거리 측정 시험 로그(nRF RTT 로그, TI UART 설정 로그)를 분석하는 절차. 사용자가 TI_IWR1843_TEST 아래 시험 폴더나 .log 파일을 주고 분석을 요청할 때 사용한다.
---

# 레이더 시험 로그 분석

## 입력

- 시험 폴더: `C:\CHOYJ_\ClaudeAI_CHOYJ\TI_IWR1843_TEST\YYMMDD_TESTn`
- 파일 이름 `0.2.log`, `0.2m.log` 등이 실제 거리(m)다. SEGGER RTT Viewer 로그이며 첫 줄들에 `Logging started @` 시각이 있다.
- `Uart_Config.txt` 또는 `config.txt`: TI 부팅 로그. auto-config가 끝까지(`sensorStart: Done`) 갔는지, SPI1로 받은 값(`SPI1: ...`)을 확인한다.

## nRF 로그 줄 형식 (`cho_test_code_260807.c`)

- `d-su, len=N`: TI 패킷 하나 = 프레임 하나(100ms)
- `numDetectedObj = N, numTLVs = M`
- `PT f=<frame> i=<idx> r=<raw mm> snr=<0.1dB> noise=<0.1dB>`: 점마다 한 줄(`RADAR_LOG_ALL_POINTS` 1일 때)
- `radar: pick pt[i]/n  range=<raw mm>  snr=<dB>  (<규칙>)`: 고른 점. 규칙은 `nearest snr>=10.0` 또는 max-SNR fallback
- `BLE_DIST_DATA(CHOYJ) : <mm>`: 보정된 BLE 출력(1초 주기)
- `<wrn> radar: only near-field points (< 187 mm calibrated) -> keep last ...`: 후보가 없어 이전 값 유지
- `PROF f=.. bNN: ...`: range profile(0.1dB, `RADAR_LOG_RANGE_PROFILE` 1이고 TI guiMonitor 3번째 인자가 1일 때만)

## 절차

1. 파일별로 다음을 요약한다. 아래 명령을 시험 폴더에서 실행하면 된다.

```bash
for f in *.log; do echo "===== $f"; grep -a "Logging started" $f
 grep -ao "\[[0-9:.,]*\]" $f | sed -n '1p;$p' | tr '\n' ' '; echo
 echo "-- picks:"; grep -ao "pick pt\[[0-9]*\]/[0-9]*  range=[0-9]* mm  snr=[0-9.]* dB  ([a-z0-9>=. ]*)" $f \
  | sed -E 's/pick pt\[[0-9]+\]\/([0-9]+)  range=([0-9]+) mm  snr=([0-9.]+) dB  \((.*)\)/n=\1 r=\2 \4/' | sort | uniq -c | sort -rn | head
 echo "-- PT:"; grep -ao "PT f=[0-9]* i=[0-9]* r=[0-9]* snr=[0-9]* noise=[0-9]*" $f \
  | awk '{split($4,a,"=");split($5,b,"=");c[a[2]]++;s[a[2]]+=b[2]} END{for(k in c) printf "  r=%s cnt=%d avgsnr=%.1f\n",k,c[k],s[k]/c[k]/10}' | sort -t= -k2 -n
 echo "-- BLE:"; grep -ao "BLE_DIST_DATA(CHOYJ) : [0-9]*" $f | awk '{print $NF}' | sort | uniq -c
 echo "-- WRN/ERR:"; grep -a -E "<wrn>|<err>" $f | sed -E 's/.*<(wrn|err)>//' | sort | uniq -c | head
 echo "-- frames:"; grep -ac "d-su" $f; done
```

2. 판정 기준
   - 보정식: `raw × 0.9986 − 96.8 mm`. 실제 거리 대비 BLE 오차가 보통 ±5mm 안이다.
   - 2R 유령: raw 약 2R − 52mm에 나온다. 타깃 대신 골랐는지 본다.
   - bin 7 정적 누설점: raw 274~278mm(보정 173~181mm), MBF 때문에 같은 거리 점이 3개로 나온다. 부팅 후 2~3분 동안만 검출된다.
   - RTT 줄 누락이나 깨진 줄(예: noise=9140처럼 비정상 값)은 펌웨어 문제가 아니다. 분석에서 제외하고 그렇게 밝힌다.
3. 로그 타임스탬프(부팅 후 경과 시간)를 꼭 함께 본다. 근거리 누설점은 거리보다 경과 시간에 좌우된다. 측정 순서와 경과 시간이 겹치면 원인을 섞어 판단하지 않는다.
4. nRF에 최신 빌드가 들어갔는지 로그로 확인한다. 예: 필터 값보다 작은 보정값이 pick되면 옛 빌드다.

## 보고

- 한국어로, 거리별 표(실제 거리, raw, SNR, BLE, 오차, 같이 나온 점)로 정리한다.
- 이번 데이터로 확인되지 않은 것(예: 부팅 직후 구간)을 따로 적는다.
- 코드 수정은 제안만 하고, 사용자가 승인한 것만 적용한다. 빌드는 제안하지 않는다.
- 분석 후 메모리 `near_range_ghost_analysis.md`에 결과를 덧붙인다.
