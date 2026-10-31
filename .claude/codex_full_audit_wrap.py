from pathlib import Path
import json

history = '''

### 414) 2026-09-08 중단된 멀티에이전트 전수조사 이슈 재확인·추적 원장
- 목표: 사용자의 “아까 전수조사에서 나온 이슈 확인” 요청에 따라 중단 당시 초안과 프로브 출력을 Codex가 직접 대조한다. 에이전트 재가동·새 제품 구현 없음.
- 변경: `docs/full_code_audit_2026-09-08.md` 신규 — host12/viewer14/server9/Android 계정5/native directory3의 초안43건과 메인 설치·입력 검토8건을 분류·보존. 총51개 검토 항목은 신규 실기 재현 버그51개라는 뜻이 아니며, 기존 D3/Android 후속 공백·옵션/오류 조건·설계 개선을 구분했다. `docs/구현계획.md`는 체크리스트 상태만 갱신.
- 직접 확인: 마지막 content frame cadence 거절 뒤 old cache 재송출(HN01), 생성 AU가 후속실패의 bool false 때문에 버려짐(HN04), restart/encoder 실패를 잃는 상태전환(HN05/06), 명시 창 실패의 다른화면 fallback(HN08), decoder 출력 metadata에 현재 입력 header를 붙이는 문제(V06), control-only traffic에서 recovery maintenance 생략(V14), 서버 credential backoff/로그 namespace/저장 성공 계약 등. 09:59 첫 UDP 단절의 물리적 원인으로 귀속하지 않았다.
- 검증: 이전 격리 산출물 result.txt/codec_partial_result.txt/server_audit_probe.out 직접 열람 및 제품 소스·호출자 대조. codec partial-output 프로브는 실제 codec+fake transform이며 실제 HW MFT 재현이 아님을 정정했다. 새 빌드/제품 테스트/실기/설치/서버 부하/배포 없음. 문서의 항목 수·링크·UTF-8·diff 범위 검사, Git MCP 문서 커밋. 기존 .claude 초안·프로브 보존, push 없음.
- 다음: latest-content 보존 → EncoderResult/provenance/reference-chain → transactional capture/encoder restart → common ReceiveMaintenance/renderer recovery → auth/storage/install 계약 순으로 설계를 확정하고 해당 실패 조건 회귀를 수행한다. 검토 완료와 제품 수정 완료를 구분한다.
'''
plan = '''

## 중단된 전수조사 재확인 후속 (원장: `docs/full_code_audit_2026-09-08.md`)

- [x] 초안43건·메인8건의 근거/조건/기존항목 구분 및 추적 문서화 (history #414)
- [ ] HN01 마지막 content frame 보존
- [ ] HN02/HN04/HN07/V06 출력·참조 체인·입력 수명 결과형
- [ ] HN05/HN06/HN08 capture/encoder 전환 실패 및 실제 대상 확정
- [ ] V07/V08/V14 렌더러 복구·수신 maintenance 공통화
- [ ] SV01/SV02/SV06/SV07/AN01/I01 인증·저장·업데이트 계약
- [ ] 나머지 옵션/Android/계측/검사 부채 우선순위 확정
- [ ] 제품 수정 및 해당 조건 회귀·실기 검증
'''
for name, text, marker in [('docs/history.md', history, '### 414)'), ('docs/구현계획.md', plan, '## 중단된 전수조사 재확인 후속')]:
    p=Path(name)
    if marker not in p.read_text(encoding='utf-8'):
        with p.open('a',encoding='utf-8',newline='') as f: f.write(text.replace('\n','\r\n'))
args={'repo_path':'D:/remote/remote','files':['docs/full_code_audit_2026-09-08.md','docs/history.md','docs/구현계획.md']}
Path('.claude/full_audit_docs_add.json').write_text(json.dumps(args,ensure_ascii=False),encoding='utf-8')
Path('.claude/full_audit_docs_commit.json').write_text(json.dumps({'repo_path':'D:/remote/remote','message':'docs: consolidate and verify interrupted full-code audit findings'}),encoding='utf-8')
print('Prepared full-code audit documentation wrap-up.')
