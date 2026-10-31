from pathlib import Path
import difflib,hashlib,json,subprocess
root=Path('D:/remote/remote')
wt=root/'.claude/worktrees/main-stability-integration'
paths=('CLAUDE.md','docs/history.md')
assert subprocess.run(['git','-C',str(wt),'diff','--quiet','HEAD','--',*paths]).returncode==0, 'preserve unreviewed document changes'
head=subprocess.check_output(['git','-C',str(wt),'rev-parse','HEAD'],text=True).strip()
before={p:(wt/p).read_text(encoding='utf-8') for p in paths}
after=dict(before)
anchor='    child 포함)은 시험 설정에서 주입을 **명시 OFF** 한다. 제품 기본값을 바꾸는 일이 아니다.'
extra='''
  - 명시 OFF 는 `enableInputInjection=false` 또는 해당 source/binary 에서 `Disabled` 로 해석됨을 확인한
    `--input-injection-mode none` 을 허용한다. 후자의 기존 `disabled (unsupported mode) mode=none` 로그는
    정확한 mode 값·이번 launch 의 effective OFF 와 함께 확인한다. 임의 unknown 값의 fallback 비활성은 인정하지 않는다.
    이 OFF 는 일반 입력 주입 경로의 차단이며 SAS/credential/clipboard 등 별도 OS 효과의 격리를 대신하지 않는다.'''
assert anchor in after['CLAUDE.md']
after['CLAUDE.md']=after['CLAUDE.md'].replace(anchor,anchor+extra,1)
line='- 다음: 기존 작업용1(Host 시험 전수 확인)·작업용2(PC e2e) 창구에서 안전 하네스/record-only sink 후보와 mode 별 OS 효과 경계를 고정해 전문 검토한다.'
extra_history='- OFF 표현 상충 해소: source 가 명시 지원하는 `--input-injection-mode none` → `InputInjectionMode::Disabled` 는 source/binary 정합·정확한 argv 값·해당 launch 의 effective OFF 확인 조건으로 인정한다. 임의 unsupported/fallback 값은 허용하지 않는다. Codex 순수 인자 parser probe 는 config 로딩 때 H.264 허용 환경 flag 가 초기화됨을 확인했고, 해당 H.264 시험 환경에서는 codec/transport 외에 `forceEncodedExperiment=true` 같은 허용 설정도 필요함을 구분했다. Host/Viewer·금지된 prelogin 시험·OS 입력은 실행하지 않았고 해당 시험의 명시 실행 금지는 유지한다.\n'
assert line in after['docs/history.md']
after['docs/history.md']=after['docs/history.md'].replace(line,extra_history+line,1)
chunks=[]
for p in paths:
    d=''.join(difflib.unified_diff(before[p].splitlines(keepends=True),after[p].splitlines(keepends=True),fromfile='a/'+p,tofile='b/'+p))
    chunks.append('diff --git a/'+p+' b/'+p+'\n'+d)
patch=root/'.claude/injection_mode_policy_reviewed.diff'
patch.write_text(''.join(chunks),encoding='utf-8',newline='\n')
subprocess.run(['git','-C',str(wt),'apply','--check',str(patch)],check=True)
assert all((wt/p).read_text(encoding='utf-8')==before[p] for p in paths)
print(json.dumps({'base':head,'patch_sha256':hashlib.sha256(patch.read_bytes()).hexdigest(),
                  'apply_check':'PASS','working_files_unchanged':True,
                  'target_utf8_lf_sha256':{p:hashlib.sha256(after[p].encode('utf-8')).hexdigest() for p in paths}},ensure_ascii=False,indent=2))
