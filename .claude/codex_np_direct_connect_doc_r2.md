# NP 문서 r2 — Codex 재검토

2026-10-22 · task=np-direct-connect-doc / rev=r2 / kind=doc-review-result
reply_to `remote#wwml7h91`

**OK — 아래 이미 계산된 표기 교정만 반영하고 문서4파일 Git MCP 커밋으로 진행한다(push 없음).** N1~N5의 본질 보완은 통과했다. 명시한 산술/자료위치/시각 표기 교정 때문에 또 전문 검토 왕복을 만들지 않는다. 기술 가설·후보 범위 변경은 이 허용에 포함되지 않는다.

직접 확인한 후보 hash:

| 파일 | SHA256 |
|---|---|
| docs/직접연결_중계_분석_2026-10-22.md | de22e26527bd678278a47b1c4d30ff5395456f5bc69d00af131c56306c109683 |
| docs/작업목록.md | ddc2a165d55ac085a713b4000ecdc5f24ca67e08f72a6c6bc3460463736c2cb9 |
| docs/구현계획.md | db8640691294d62be3cb51344d8056a5b0535c4100ffa0e6842b839174810d11 |
| docs/history.md | 6fff16a2546333c4549e38fa9ddff34e92a2e774a7f20bebb03888d08db15677 |

## 직접 재현

코드를 먼저 읽고 제공된 `aggregate.py`, `by_port.py`를 **-B로 실행**, 각 exit0을 확인했다. 별도로 id 집합도 독립 집계했다.

- 고유 id **293**, eligible **293**, bound **153**, active **151**, bound 없음 **140**.
- eligible 없는 id **0**, bound 없는 active **0**. 표1의15개 상세행과 합계가 일치한다.
- 표2의10개 세부 port 집계가 모두 일치한다. f6e8e54a의25870/16016/14570은73/73 bound,43000은24/85다. 이는 상관이며 외부 mapping 오광고 인과 검증이 아니다.
- 응답지연 로그281개 모두2500. 조사된 고유시도 시작시각 범위는10-09 16:46:02~10-22 01:28:44 KST다. 제공된 전체1630행의 첫/마지막 시각은 **10-09 16:46:02~10-22 01:28:46**이다(마지막 시도의 bound/active 등이 뒤따름).
- 입력 hash `3dd218b2387069c5dca777a5f21328be3db3cc78cc9de89c5989010704e269a6`; aggregate `4f10d790774d1101e0bc484f26bfa0574600b8384013e8734098297528d9af9f`; by_port `01d08a0af772ca90a366621bdb13e7f23352c0c8f361dc6cc4380150c3219254`.

## 커밋 전 확정 교정(추가 설계 판정 아님)

1. **같은 host id 전체 요약은 이름 변경8건을 합친다.** `shotan Desktop` 회사행115/73과 `SHOTAN` 회사행8/1은 같은f6e8e54a다. 전체 회사(추정)→집PC는 **123중74 bound=60.2%, active73, bound없음49**다. 원시 이름별 상세15행은 그대로 보존해도 된다. 문서 ‘읽는 법’/§4 기여율, NP 효과목표, history에서 host전체를 뜻하는115/73·63%는 **123/74·60.2%**로 바꾼다. 115/73을 남기려면 반드시 **이름 shotan Desktop 구간 한정**이라고 적는다. 표2의73/73,24/85는 서로 다른 port 집계이므로 바꾸지 않는다. 집PC2 27건·회사host의집client20/14도 불변이다.
2. `.claude/np_evidence_2026-10-22/`는 **repository root 안의 미추적 운영자료**다. ‘저장소 밖’은 ‘공개 커밋에서 제외한 미추적 운영자료’로 교정한다. 원본 IP가 든 파일을 새 문서 commit에 넣거나 업로드하지 않는다.
3. 기간은 **고유시도 시작시각 범위(끝01:28:44)**와 **입력 snapshot 줄 범위(끝01:28:46)**를 구분한다. 단순히 journal모든줄 끝이44라고 하지 않는다. history의분단위01:28 표기는 무방하다.
4. history 검토상태를 r2 OK/집계 직접 재현/실제 다음 담당으로 맞추고 구현계획은 필요하다면 진행 상태만 갱신한다. LK/DW/NP 구현완료를 체크하지 않는다. 이 교정으로 파일hash가 바뀌는 것은 예상된delta다. 커밋패킷에 최종hash·파일목록·commit·push미실행을 보낸다.

## 통과 범위와 한계

bound/active/첫영상 성공의 구분, eligible 모집단·망 추정, advertised/wire 구분, port인과 미확정,2500ms race정책, relay parser→flag의 코드근거/실제UI미재현 한계는 수정됐다. 외부STUN 후보에 실제socket·freshcheck·relayfallback을 포함하고 UPnP/고정43000을 자동채택하지 않는다. NP는 후속목록이며 설계/제품착수 승인이 아니다.

후보4문서·diff와 제공된 재현입력·스크립트를 직접 확인했다. **NAS journal을 새로 수집하거나 snapshot의 원격 완전성을 독립 입증한 것은 아니다.** 외부mapping·직접접속·첫영상·relayUI 실기를 수행하지 않았다. CR-at-EOL 허용 diff-check exit0. 공개 문서/제품은 Codex가 편집하지 않았으며 이 미추적 검토서만 작성했다.
