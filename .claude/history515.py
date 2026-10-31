# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 515) 2026-09-11 `0.2.114` 게시 — **Client 업데이트 확인 수정이 실린 첫 릴리스**
`6d1d422`(Client 확인 배선) 를 담은 Windows 단독 후보. **Android 무변경.**

**바뀐 바이너리**
| 파일 | 크기 | sha256 | 비고 |
|---|---|---|---|
| **`GNLinkClient.exe`** | **711,168** | `6ff70eb3489e9ae1…` | 113 의 707,584 → **+3,584B, 확인 배선이 여기 있다** |
| `GNLinkHost.exe` | 660,480 | `3ee51b419e18bd04…` | |
| `GNLinkUpdater.exe` | 548,352 | `17ecfa1eaf8306f5…` | |
| `GNLinkSetup.exe` | 4,371,968 | `c095a0e91e7499ea…` | |
| `GNLinkStream.exe` | 1,000,448 | `a23a656cd2518799…` | 113 과 바이트 동일 |
| `GNLinkCapture.exe` · `GNLinkInputService.exe` · `GNLinkViewer.exe` · `ui\\shell.html` · `ui\\macro.html` | | | 동일 |
- manifest **1,687B `be00ba15feef6f6e…`**(artifact 10줄, `releaseId=r-0.2.114`) · `windows.sig` 자체검증 Ok
- `dist/GNLinkSetup-0.2.114.exe` 4,371,968 `c095a0e9…`

**검사**
- **Release 전량 재빌드 후 sweep**: `config=Release built=2026-09-11T12:29` **66 스위트 `^PASS` 1861 · 실패 0**(65→66 은 `client_update_flow_test` 신설). 검증용이 요구한 "전체 sweep 갈음" 항목이고 **config·빌드 시각을 함께 기록**했다(`.claude/sweep-per-suite-release.txt`).
- 설치기 임베드 **9/9**(Setup 은 자기를 임베드할 수 없어 9개, manifest 의 10번째가 Setup)
- preflight **payload-set 게이트 통과** · manifest ↔ 디스크 **10/10**
- 제품 C++ `default_verifier()` — **게시 전 문서** Ok / 변조 SignatureInvalid, **게시 후 서버에서 되받은 바이트**도 서명본과 **바이트 동일**
- **서버 10파일 크기·sha256 10/10 일치** · **공개 URL 10개 전부 200 + 해시 일치**
- **재시작 0 · env 0** · 백업 `manifest-backups/0.2.113/` = `4ff44f97…` + `5784928d…`
- 현재 백업 목록: `0.2.108` · `0.2.109` · `0.2.109-20260910T151705Z` · `0.2.110` · `0.2.111` · `0.2.113`.
  ⚠️ **`0.2.112` 가 없는 것이 맞다** — 그것은 수동 설치 전용 빌드였고 게시된 적이 없다.

**사용자 전달 경로 (한 번만 안내)**
회사 PC 의 0.2.112 는 **자동 확인이 고장나 자기 자신을 고칠 수단이 없다.** 두 길뿐:
**(a)** 그 PC 에 Host 가 설치·로그인돼 있으면 **Host 트레이 → 업데이트 확인** 한 번 — `payloadNames` 10개에 `GNLinkClient.exe` 가 있어 **Client 도 같이 교체된다**(이 PC 에서 `c71864fd`→`7e583f6c` 실측).
**(b)** 아니면 **설치본 수동 1회**.
**회사 PC 무단 조작·로그인 없음. 재실행 반복 요구 없음.**

**여전히 미검증 / 별건 (지시대로 손대지 않음)**
- **Host 고아 자식** — 물러나는 Host 가 자식을 남겨 포트를 쥐고 다음 기동을 막는다. 오늘 두 번 기기를 불능으로 만들었고, 복구는 사용자가 손으로 고아를 죽여서 됐다. **이 릴리스에도 그 결함은 그대로 있다.**
- 11:48 업데이터 **트리거 정체**(Host·Client·작업용 모두 아님) · **`result:` 부재 = `effects.run()` 미반환** · `CREATE_BREAKAWAY_FROM_JOB` **성공 시 무기록**
- **진입점(트레이 클릭 → 핸드오프 → ack) 포함 인앱 완주** — 아직 한 번도 확인되지 않았다
- Client 재기동 시 **토큰 승격 여부**(`IsElevated=false` · integrity Medium) — 재시험 항목
- https 실전송 · WebView 렌더
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
