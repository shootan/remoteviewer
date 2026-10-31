from pathlib import Path
import difflib,hashlib,json,subprocess
root=Path('D:/remote/remote');wt=root/'.claude/worktrees/main-stability-integration'
paths=('CLAUDE.md','docs/history.md')
assert subprocess.run(['git','-C',str(wt),'diff','--quiet','HEAD','--',*paths]).returncode==0,'preserve pending document edits'
head=subprocess.check_output(['git','-C',str(wt),'rev-parse','HEAD'],text=True).strip()
before={p:(wt/p).read_text(encoding='utf-8') for p in paths};after=dict(before)
old='    이 OFF 는 일반 입력 주입 경로의 차단이며 SAS/credential/clipboard 등 별도 OS 효과의 격리를 대신하지 않는다.'
new='    이 OFF 는 일반 입력 주입 경로의 차단이며 SAS/credential/clipboard·display wake/전원 유지 등 별도 OS 효과의 격리를 대신하지 않는다.'
extra='''
  - 실 Host 시험은 stream-start wake 입력·monitor-power broadcast·display/system keepalive 도 명시 OFF 또는
    검토된 record-only OS 효과 경계로 차단한다. 입력 OFF 만으로 전원/화면 효과까지 없다고 주장하지 않는다.
    로컬 시작 정책은 원격 제어로 켤 수 없어야 하고, 객체 생성·기동의 최초 효과 전에 적용돼야 한다. 지원 여부와
    OFF 인자를 기동 전에 확인하고 그 launch 의 effective 상태를 연결/스트리밍 전에 확인한다. ON/미확인은 중지·invalid 다.
    ON/누락/오타 부정 대조도 실제 OS 효과가 없는 sink/격리 환경 안에서만 한다. 구 바이너리가 차단을 지원하지 않으면
    승인된 격리 환경/명시된 test build 를 쓰거나 해당 case 를 미검증으로 남긴다. 이름 alias·offscreen 은 효과 차단이 아니다.'''
assert old in after['CLAUDE.md'];after['CLAUDE.md']=after['CLAUDE.md'].replace(old,new+extra,1)
old='(eventKind=6, key-down·마우스·text 0)은 작업용2 로그 집계다.'
new='(eventKind=6, 그 ControlInput timing 집계 안에서 key-down·마우스·text 0)은 작업용2 로그 집계다. 이 집계는 별도 wake-display 경로의 직접 OS API 호출을 포함하지 않는다.'
assert old in after['docs/history.md'];after['docs/history.md']=after['docs/history.md'].replace(old,new,1)
anchor='- 다음: 기존 작업용1(Host 시험 전수 확인)·작업용2(PC e2e) 창구에서'
line='- Finding-3: HostPowerKeepalive 의 stream-start wake 는 입력 스위치와 별도로 synthetic mouse 이동/모니터 전원 broadcast 를 호출하며 display/system execution-state 요청도 있다. 생성자 Apply(false)는 인자 파싱 전 실행된다. 과거 실제 커서/모니터 반영·호출 횟수는 미확인으로 유지한다. 기본 ON·로컬 시작 전용·원격 변경 불가인 중앙 전원 정책과 효과 직전 차단을 설계하며, 테스트는 입력과 전원 양쪽 OFF/기록 경계를 고정한다. 단순 wake 함수 차단만으로 power 효과0을 주장하지 않는다. 실 Host 스트리밍 EVIDENCE 의 안전한 재확인 대기 범위에 반영하고 9개 시험 보류·기존 prelogin 실행 금지를 유지한다.\n'
assert anchor in after['docs/history.md'];after['docs/history.md']=after['docs/history.md'].replace(anchor,line+anchor,1)
out=''
for p in paths:
 d=''.join(difflib.unified_diff(before[p].splitlines(keepends=True),after[p].splitlines(keepends=True),fromfile='a/'+p,tofile='b/'+p))
 out+='diff --git a/'+p+' b/'+p+'\n'+d
patch=root/'.claude/injection_power_policy_reviewed.diff';patch.write_text(out,encoding='utf-8',newline='\n')
subprocess.run(['git','-C',str(wt),'apply','--check',str(patch)],check=True)
assert all((wt/p).read_text(encoding='utf-8')==before[p] for p in paths)
print(json.dumps({'base':head,'patch_sha256':hashlib.sha256(patch.read_bytes()).hexdigest(),'apply_check':'PASS','working_files_unchanged':True,'target_utf8_lf_sha256':{p:hashlib.sha256(after[p].encode('utf-8')).hexdigest() for p in paths}},ensure_ascii=False,indent=2))
