p = 'D:/remote/remote/.claude/caplife/report_gate_temp_T.md'
s = open(p, encoding='utf-8').read()
reps = [
    ("- 실행 전후에 %TEMP% 의 `gnlink-fixed-server-gate-*` 개수를 읽기만 했다: 0 → 0. 탐색·정리는 없다.",
     "- %TEMP% 에 대해서는 **제한된 조회만** 했다: 실행 전후 `gnlink-fixed-server-gate-*` 패턴의 개수만 세었고, 결과는 0 → 0 이었다. "
     "그 밖의 탐색·정리·복구는 0 이다. 이 조회를 \"%TEMP% 를 전혀 읽지 않았다\" 로 쓰지 않는다. "
     "또 현재 값으로 과거의 무영향을 판정하지 않는다."),
    ("- **이 세션(36e9662b, 작업용)의 self-test 실행 호출: 8회**. 이 문서 작성 중에 확인했다. 앞선 보고의 \"1회\" 는 57fa793 검증분 하나만 센 것이므로 **정정한다**.",
     "- **이 세션(36e9662b, 작업용)에서 self-test 를 부른 도구 호출: 8건.** 이 문서 작성 중에 확인했다. 앞선 보고의 \"1회\" 는 57fa793 검증분 하나만 센 것이므로 **정정한다**.\n"
     "  - 이 8 은 **호출 수**다. 한 호출 안에서 self-test 가 실제로 몇 번 돌았는지, 각 실행의 check 수, 만든 폴더의 경로, 삭제 결과는 **미확인**이다."),
    ("은 **43건, 8개 세션**이고, 기간은 2026-09-29 ~ 2026-10-28 이다.",
     "은 **43건, 8개 세션**이고, 기간은 2026-09-29 ~ 2026-10-28 이다. "
     "이것도 **도구 호출 수**다. 실제 self-test 실행 횟수·check 수·경로·삭제 결과와 같은 값이 아니다."),
]
for a, b in reps:
    assert s.count(a) == 1, a[:50]
    s = s.replace(a, b)
s += '''
## r2 추가 — T1: cleanup 이 junction 에 들어가지 않음 (Codex codex_avd_guard_src_r3.md)
- ka-r3 **af49c26ff8fff967003433d07126f58384f93cc3** (부모 8a84198). feat 병합 **9f9a91c** (충돌 0).
- 순회 방식: top-down 으로 걷되, 각 하위 폴더는 **내려가기 전에** 판정한다. link(symlink·junction)는 순회에서 제외(prune)하고, 그 자리에 남기고, 출력한다.
- 삭제 순서: 남은 파일을 지운 뒤, 폴더를 깊은 순서부터 지운다. cleanup root 자체가 일반 폴더가 아니면 아무것도 건드리지 않는다.
- 대조(root_test **20/20**, ka-r3 와 feat 병합본 각각):
  - 실제 directory junction 을 이 시험 소유의 sibling 폴더(sentinel 있음)로 만들었다.
  - **보수 전**(git 의 8a84198 cleanup) 을 삭제 기록 대역으로 돌렸다(실제 삭제 0). junction **아래의 sentinel 을 선택했다** = 보수 전 FAIL 재현.
  - **보수 후** 를 같은 기록 대역으로 돌렸다. junction 아래 선택 0, run 자신의 파일만 선택했다.
  - 실제 cleanup: sentinel 과 junction 이 남고, junction 을 출력했고, run 을 깨끗하다고 하지 않았다(비성공).
  - 정리: junction 은 **link 자체만** 제거했다(`os.rmdir(junction)`, 재귀 삭제 없음). sentinel 은 무변경이었다.
- 이 root_test 안에서 보수된(T1) self-test 가 자기 root 에서 53/53, 잔여 0 으로 실행됐다. 보수 전 self-test 는 실행하지 않았다.
- **commit 메시지 정정**: 8a84198 메시지의 anchor 경로 한 줄(`D:emoteemote...`)은 셸 escape 로 깨진 것이다. 뜻한 경로는 `D:\\remote\\remote\\.claude\\test-tmp` 다. af49c26 메시지에 정정을 기록했고, amend 는 하지 않았다.
'''
open(p, 'w', encoding='utf-8').write(s)
print('ok')
