base = 'D:/remote/remote/.claude/'

def append(n, text):
    s = open(base + n, 'rb').read().decode('utf-8')
    if '\r\n' in s:
        text = text.replace('\n', '\r\n')
    open(base + n, 'wb').write((s + text).encode('utf-8'))

append('report_apk_prelogin_select_r1.md', '''

## r3 — Codex codex_apk_prelogin_select_src_r2.md 잔여 N1 (poll 의 완료 vs 취소/새 Prepare, 2026-10-27)
**고정: `fix/apk-prelogin-select` 9afcec7 (제품) · 3a17444 (시험)**
- 9afcec7: `ClientEncodedFrameSink::CommitLocalSelectionFor(tag, commit)` 신설 — "아직 이 선택인가 · G 로 결합됐나" 판정과 panel 기록(`ApplyLocalSelectedFor`)을 **sink 의 선택 상태 잠금 하나 아래** 한 단계로. Android sink 는 `LockedState`(nativePrepareVideoSwitch/nativeAbortVideoSwitch 가 잡는 바로 그 잠금) 안에서 commit 을 부른다 → 취소/새 Prepare 는 통째로 앞(아무것도 완료 안 됨) 또는 통째로 뒤(이미 기록된 완료를 그 뒤에 무효화). `PollLocalSelection` 은 이 한 호출만 쓴다(재읽기 추가 아님). 잠금 순서 local → sink → panel 한 방향; sink→local 경로 없음.
- 3a17444: P9 — 시험 hook `SetTestBeforeLocalCommitHook`(시험 빌드 전용)로 poll 을 "bound 확인 뒤·panel 기록 전" 에 세우고, 그 틈에 UI 스레드가 취소(Abort) 또는 새 Prepare B 를 호출(최대 600 ms 기회). FAIL 조건: UI 호출이 끝난 **뒤에** A 가 selected 로 기록됨. B 는 자기 IDR(701)로 완료.
- 결과: `client_session_prelogin_select_test` **56/56**(rc0). 변이(r2 poll 모양 — 읽고 나서 sink 밖에서 기록) → P9 경계 2건 **FAIL**(`at ui done=0 final=window_selected`), 복원 해시 일치 후 56/56. `role_park` 13/13, `shared_core` rc0.
- 미검증: 실 APK 에서 이 틈을 재현하지는 않았다(소스 경계 + 제품 Android sink 와 같은 모양의 GateSink 시험).
''')

append('report_rel_apk_0232_r1.md', '''

## r3 — 통합 후보 갱신 (picker N1 마지막 경계, 2026-10-27) — 이 절이 r2 후보를 대체한다
**고정 후보: `fix/apk-prelogin-select` 3a17444** (05fe4b9 위 9afcec7·3a17444, tracked clean). 버전 0.2.32 / 31 그대로(미게시).
- APK: `D:\\remote\\remote\\.claude\\rel-apk-0232\\GNLink-0.2.32.apk` (덮어씀; r2 의 b43a94ab… 는 더 이상 후보 아님)
  - **13,973,100 B, sha256 `ece254f00393400d5d82bd54d2d30ce084552e02c378df766963491933db8e4c`**
- noseam `clean assembleDebug testDebugUnitTest` rc0, 단위 263/0 (`caplife\\apk\\gradle_0232r3.txt`).
- aapt versionCode 31 / 0.2.32, 4 ABI; apksigner V2 `dcc806ae…2990`; 게이트 ALL PASS(7).
- AVD smoke(이 APK 그대로): `method_ui` 16/16, 슬롯 삭제 확인, 잔여 프로세스 없음.
''')
print('ok')
