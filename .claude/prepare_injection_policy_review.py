from pathlib import Path
import difflib
import hashlib
import json
import subprocess

root=Path('D:/remote/remote')
wt=root/'.claude/worktrees/main-stability-integration'
paths=('CLAUDE.md','docs/history.md')
actual=subprocess.check_output(['git','-C',str(wt),'diff','--',*paths]).decode('utf-8').replace('\r\n','\n')
submitted=(root/'.claude/injection_policy_r1.diff').read_text(encoding='utf-8').replace('\r\n','\n')
assert actual==submitted, 'working diff differs from submitted r1; preserve and review the new snapshot'
before={p:(wt/p).read_text(encoding='utf-8') for p in paths}
after=dict(before)
old='  이라 시험 Host 가 이 PC 실제 입력으로 key-up 을 보낸 사건, `.claude/codex_test_injection_finding.md`).'
new='  이라 시험 Host 의 입력 주입 경로가 켜져 있었던 사건(OS 실제 영향 미확인), `.claude/codex_test_injection_finding.md`).'
assert old in after['CLAUDE.md']
after['CLAUDE.md']=after['CLAUDE.md'].replace(old,new,1)
anchor='    를 찍고 계속하는 것은 fail-closed 가 아니다. 사후 grep 은 사전 gate 를 대신하지 않는다. 부정 대조(설정 누락/true·\n    확인 미도달)에서 연결 0 을 확인한다. 자기 launch 의 자식만 회수한다.'
replacement=anchor+'\n  - 실행 중 ON/상충 상태가 발견돼도 그 run 을 중지·invalid 로 기록한다. 사후 확인만으로 사전 gate 누락을 면제하지 않는다.'
assert anchor in after['CLAUDE.md']
after['CLAUDE.md']=after['CLAUDE.md'].replace(anchor,replacement,1)
anchor='  - OFF 여도 Host 의 input 수신·ACK 경로는 살아 있다 — 전송·수신·순서·fence 는 OFF 로 검증하되, 그것을 OS 주입\n    성공으로 보고하지 않는다.'
extra='''
  - 정책/routing 시험의 record-only sink 는 제품 수신·gate·dispatch 를 공유하고 물리 OS 주입·실 broker 위임·사용자
    Default fallback 을 모두 차단한다. 논리 dispatch 와 OS 효과 OFF 를 구분해 이번 build/process 에서 확인하고,
    Host 의 허용/거부·sink 도달을 검사한다. Queue/ACK 만으로 정책 통과나 OS 주입 성공을 주장하지 않는다.
  - **viewer_prelogin_login_e2e_test 현 하네스는 전 모드·wrapper 실행 금지**(Finding-2). 수정·빌드·정적/비해당
    안전 시험은 계속할 수 있으나, 재설계·효과 OFF/sink 검토·이 문서 commit 으로 금지가 풀리지 않는다. 동일 시험은
    **사용자의 명시 재개 지시 전까지 재실행하지 않는다.** 별도 desktop/window station·Default 이름 alias 자체를
    입력 격리나 실제 GPU/Winlogon 동등성의 증거로 쓰지 않는다.'''
assert anchor in after['CLAUDE.md']
after['CLAUDE.md']=after['CLAUDE.md'].replace(anchor,anchor+extra,1)
heading='### 2026-10-28 안전 Finding — 시험 Host 입력 주입 기본 ON (정책 문서)'
i=after['docs/history.md'].index(heading)
assert after['docs/history.md'][i:].count('\n### ')==0, 'preserve later history entries'
after['docs/history.md']=after['docs/history.md'][:i]+'''### 2026-10-28 안전 Finding — 시험 Host 입력 주입 기본 ON (정책 문서)
- 목표·변경: CLAUDE.md '검증 독립성과 재현' 절에 시험 Host 명시 OFF + 해당 launch 의 effective OFF 확인 전 연결 금지(fail-closed), OS 효과 경계 및 특정 시험 실행 금지를 기록. 변경 파일은 CLAUDE.md·docs/history.md 두 개이며 제품 기본값은 변경하지 않음.
- 사실·확인 한계: GNLinkStream 기본은 enableInputInjection=true·background_message. KA PC e2e(r1~r4)의 주입 ON·modifier key-up 2490(eventKind=6, key-down·마우스·text 0)은 작업용2 로그 집계다. OS 실제 영향은 미확인이고 osInjectionConfirmed=0 은 무영향 증거가 아니다. KA host_standby_admission_e2e 의 검증용 4회는 주입 여부가 로그 삭제로 미확인이다. 다른 Host 시험의 OFF 전제도 전수 조사 중이다.
- Finding-2: viewer_prelogin_login_e2e 는 사용자 Default 를 logon 대역으로 쓰고 Tab down/up·pointer 를 전송하는 경로가 있다. 검증용의 10-26~27 PS/LK·기본 prelogin·android-select·render 등 실행 보고를 조사 목록으로 보존하며, 모드별 실제 입력/화면 반영은 미확인이다. 현 하네스 전 모드·wrapper 는 실행 금지·재설계 검토 대상이고 사용자 명시 재개 전에는 실행하지 않는다.
- 기존 판정 정정: KA 82679b2·534242b 의 실 GNLinkStream e2e 는 안전한 재확인 전 관문 사용 보류, 순수 시험·NP e16ad0d 구성 단위 EVIDENCE 는 유지. KA PC③는 최종 EVIDENCE 승인이 없었음. PC 0.2.163 PS/LK 및 동일 하네스를 사용한 행동, APK 0.2.32 android-select/prelogin 근거는 관측 원문을 보존하되 관문 사용 보류/안전한 재확인 대기로 정정한다. 제품 SOURCE·서명/hash·게시/main/dist 사실은 별개로 유지한다.
- 검토: Codex `codex_test_injection_finding.md` 의 정책/영향 판정과 감수 완료 diff(`injection_policy_r1_reviewed.diff`)를 대조한 문서 전용 변경. 금지된 시험·제품 build·OS 입력은 실행하지 않았다. 정책 문서 검토는 해당 시험의 실행 재개 승인이 아니다.
- 다음: 기존 작업용1(Host 시험 전수 확인)·작업용2(PC e2e) 창구에서 안전 하네스/record-only sink 후보와 mode 별 OS 효과 경계를 고정해 전문 검토한다. 현 시험의 금지는 유지하며 비해당 안전 작업은 계속한다. 이 두 파일만 Git MCP commit, 단독 push 없음.
'''

def diff_text(a,b):
    chunks=[]
    for p in paths:
        d=''.join(difflib.unified_diff(a[p].splitlines(keepends=True),b[p].splitlines(keepends=True),fromfile='a/'+p,tofile='b/'+p))
        if d: chunks.append('diff --git a/'+p+' b/'+p+'\n'+d)
    return ''.join(chunks)
base={p:subprocess.check_output(['git','-C',str(wt),'show','6fed2a3:'+p]).decode('utf-8').replace('\r\n','\n') for p in paths}
fix=root/'.claude/injection_policy_r1_review_fixes.diff'
full=root/'.claude/injection_policy_r1_reviewed.diff'
fix.write_text(diff_text(before,after),encoding='utf-8',newline='\n')
full.write_text(diff_text(base,after),encoding='utf-8',newline='\n')
subprocess.run(['git','-C',str(wt),'apply','--check',str(fix)],check=True)
for p in paths:
    assert (wt/p).read_text(encoding='utf-8')==before[p], 'review must not edit the working files'
print(json.dumps({'amendment_apply_check':'PASS','working_files_unchanged':True,
                  'reviewed_diff_sha256':hashlib.sha256(full.read_bytes()).hexdigest(),
                  'target_utf8_lf_sha256':{p:hashlib.sha256(after[p].encode('utf-8')).hexdigest() for p in paths}},ensure_ascii=False,indent=2))
