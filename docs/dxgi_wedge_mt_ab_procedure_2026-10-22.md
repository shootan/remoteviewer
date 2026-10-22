# DXGI wedge — D3D 멀티스레드 보호 A/B 회사 PC 실기 절차 (dxgi-wedge-fix r1, Codex B1)

**상태: 시험 빌드만 준비됨. 출하 변경 없음. 실기는 사용자 조율 후에만 한다.** 이 문서는 절차와
산출물 위치만 정한다. 결과가 나오기 전에는 "보호 on 이 원인을 고친다"고 주장하지 않는다.

## 왜

10-08~10-21 회사 PC(NVIDIA) wedge 9건은 모두 `d3dMt=off` DXGI 구간에서 났다(0.53 h). `d3dMt=on`
DXGI 3.24 h 와 WGC 2.48 h 에서는 0건이었다. 다른 NVIDIA 호스트(3be5148e)는 off 에서 4건, AMD 호스트
(8ec6ecb1)는 off 7.0 h 에서 0건이었다. 이것은 상관관계다. on 구간은 거의 전부 "WGC 로 시작한 뒤 DXGI
전환" 경로라서 장치 생성 경로 차이가 섞여 있다. 같은 생성 경로에서 보호만 바꿔 비교해야 원인을 가를
수 있다.

## 시험 빌드

- `GNLinkStreamMtAb.exe`: CMake target `remote60_native_video_host_mtab_seam`. 소스와 라이브러리는
  GNLinkStream 과 같고, `REMOTE60_MT_AB_TEST_SEAM` 만 다르다(`apps/native_poc/src/mt_ab_test_seam.hpp`).
  설치 프로그램에는 들어가지 않는다. 출하 gate `automation/gnlink_check_fixed_server.py` 가 출하
  exe 에 `mt-ab` 표식이 있으면 거부한다.
- arm 은 exe 옆 `GNLinkStream.mt-ab.txt` 한 줄로 정한다.
  - `on`: 장치 생성 직후, MF·readback·WGC·DXGI 가 쓰기 전에 `SetMultithreadProtected(TRUE)` 를 호출한다.
  - 그 밖의 값이거나 파일이 없으면: 출하와 같다. 강제로 off 로 만들지 않는다.
  - 로그 한 줄이 남는다: `[native-video-host][mt-ab] arm=<on|off> before=<0|1> after=<0|1|unknown>`.
    `after` 는 `GetMultithreadProtected` 로 읽은 값이다. Set 의 반환값은 호출 전 상태이므로 쓰지 않는다.
  - 장치를 다시 만들 때도 같은 arm 을 적용하고 같은 줄을 남긴다.

## 절차 (회사 PC, 사용자가 함께)

사전 조건: 사용자 승인. 설치된 GNLinkHost 를 멈추고, exe 를 교체하고, 다시 시작하는 일은 모두 사용자
기기 조작이다.

1. **기준선 기록**: 설치 폴더 `GNLinkStream.exe` 의 SHA-256, 버전, GNLinkHost/GNLinkStream 의 pid·시작
   시각, 원본 복사본 위치를 적는다.
2. **교체**(관리자): GNLinkHost 를 트레이에서 종료한다. 원본 `GNLinkStream.exe` 를 백업한다.
   `GNLinkStreamMtAb.exe` 를 `GNLinkStream.exe` 이름으로 복사한다. 같은 폴더에 `GNLinkStream.mt-ab.txt`
   를 만든다. 첫 arm 은 동전 던지기로 정하고 기록한다.
3. **한 회(≈10 분)**:
   - GNLinkHost 를 시작한다. 반드시 **호스트 재시작 직후의 첫 DXGI 세션**이어야 한다. 이 수정의 격리
     기록이 있으면 WGC 로 시작하므로, 실기 동안은 `C:\ProgramData\GNLink\capture-recovery\` 를 비운다
     (관리자, 백업 후).
   - 뷰어로 접속해 데스크톱을 고르고 평소 게임(도깨비의세계 등)을 실행한다. 해상도·전체 화면 조건은
     회마다 같게 한다.
   - 과거 재발이 늦게는 350 s 였으므로 10 분 동안 쓴다. wedge 가 나면 그 시각과 함께 그 회는 끝난다.
4. **교차**: arm 을 on/off 번갈아 바꾸며(on,off,off,on,... 순서 편향을 줄인다) **arm 마다 최소 5회** 한다.
   arm 을 바꿀 때마다 txt 를 고치고 호스트를 재시작한다.
5. **원복**: 백업한 원본 exe 로 되돌린다. 해시가 1단계 값과 같은지 확인한다. txt 와 capture-recovery
   백업도 원래대로 둔다.

중단: 사용자가 원하거나, 게임·업무에 지장이 있으면 그 자리에서 원복한다. 중단과 시간 미달은 그대로 적는다.

## 비교 지표 (NAS host/viewer 로그, 회 단위)

| 지표 | 출처 |
|---|---|
| wedge 수, 시각, phase | `dxgi-worker-wedge`, `self-terminated (dxgi-worker watchdog)` |
| 3 s 경고 시점 화면 상태 | `[os-snapshot]` (이 수정의 Bⓒ) |
| arm 적용 확인 | `[mt-ab] arm=... after=...`, `d3d-mt at=device-created` |
| 첫 프레임까지 시간 | `capture-switch first-frame` |
| readback 정체 | `readbackWaitUs`, `capture-stamp clamped behind-synthetic` 횟수 |
| 입력→화면 지연 p50/p95 | viewer `[input-timing]`/`user-feedback` (정의는 출처 코드 기준으로 고정한 뒤 집계) |
| CPU·프레임률 | host 30 s 통계 줄, viewer `decodedFrames` |

AMD 대조: 이 PC(8ec6ecb1)에서 같은 절차를 arm 마다 2회씩 해서 회귀만 본다(재현 대상이 아님).

**판정 원칙**: on 에서 0건이어도 원인이 확정되지는 않는다. off 재현율과 함께 보고, 출하 반영 여부는
Codex 가 판정한다.

## 산출물 위치

- 시험 exe: `<build>\apps\native_poc\Release\GNLinkStreamMtAb.exe`. 실기 때는 hash 를 적어 전달한다.
- 실기 기록: `D:/remote/remote/.claude/mt_ab_field_<날짜>.md`. 회마다 arm·시작/끝·wedge·지표·해시를 적는다.
