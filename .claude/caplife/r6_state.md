

## r6 — Codex codex_sh_strength_r5.md N1-a · N1-b (2026-10-27) — ⏸ 사용자 "일단 대기" 로 멈춤
**커밋: `fix/apk-prelogin-select` d29a034 (제품) · b554e76 (시험)** — tracked clean, WIP 없음.
- a) `SharpenOutputLog` 의 출력 token 을 프로세스 전역 카운터 하나에서 받는다. renderer A/B 가 같은 token 을 가질 수 없다.
- b) `SharpenLatch.qualify`(Activity 가 쓰는 판정):
  - 디코드 프레임의 **첫 화면 표시** = 그 프레임의 버퍼(VIDEO, 또는 VIDEO 가 latch 안 됐을 때의 REDRAW) 중 처음 latch 된 것. 1회만 센다.
  - 이후 같은 프레임은 redraw 로 센다.
  - CLEAR·unknown 은 그림이 아니며 `NONE_SHOWN` 이다(이전 proof 를 재사용하지 않아 fence 가 유지된다).
- 단위 277/0. 변이(각각 FAIL):
  - log 별 counter → 2
  - REDRAW 는 첫 표시 불가 → 1
  - clear 가 이전 proof 유지 → 4
- **하지 않은 것(대기 지시)**: 새 후보 APK 빌드, AVD 배치, apk0232_verify.md 갱신.
  - 검증용 배치 뒤 자동 시작하도록 예약했던 작업은 아무것도 시작하기 전에 취소했다.
  - 후보 파일은 여전히 1d4a05f4… 이고, superseded 사본도 만들지 않았다.
  - 따라서 1d4a05f4 후보에는 r6 가 **들어 있지 않다**.
- 재개 시: `caplife\\rel0232_r6_after_verifier.ps1`(또는 `rel0232_r6_batch.ps1` 바로) → 새 해시 → verify 문서·rel 보고 갱신 → pin.
