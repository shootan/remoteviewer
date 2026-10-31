p = 'D:/remote/remote/.claude/caplife/report_inj_draft.md'
s = open(p, encoding='utf-8').read()
start = s.index('**하네스·launch 별 표**')
end = s.index('| viewer_prelogin_login | — | — | — | 실행 금지(무변경) | — |')
end = s.index('\n', end) + 1
table = '''**하네스·launch 별 표**(ka-r3 7205206 기준 file:line. "연결 0" = 실패 시 client 생성 전 종료)
- 두 열은 서로 다른 질문이다. **exe 열**: 그 실행 파일이 OS 효과(입력·전원·wake)를 실제로 수행하는 build 인가. **도달 열**: guard 이전 실행에서 그 launch 가 생성자 power 요청과 stream wake 경로에 실제로 이르렀는가.
- 도달 열은 **소스 순서에 따른 정적 판단이며 관측이 아니다.** 생성자 power 는 main() 이 HostPowerKeepalive 를 만들 때다. stream wake 는 첫 capture 시작(첫 client Hello 뒤, `streamControlActive` 기본 true)과 그 뒤의 경계다. "모든 시험이 실제로 wake 했다" 는 일괄 관측으로 쓰지 않는다.
- r5 현재: 아래 guard launch 는 모두 `--host-power-management off` 다 → 생성자 power 요청 0, wake 0. 근거는 정책 bind 순서와 record-only 순수 시험이다(실행 전).

| 시험 | launch / case | argv(r5) | 이번 launch OFF 확인 | 실패 시 | client 입력 | exe — OS 효과 수행? | guard 전: 생성자 power 도달 | guard 전: stream wake 도달 |
|---|---|---|---|---|---|---|---|---|
| host_standby_admission_e2e | StartHost: standby·realdir·relay·… | mode none + power off (`:317`) | StartHost `:388`(pipe), adopt `:369` | INVALID exit 2, 연결 0 | 없음 | 제품 GNLinkStream.exe — **수행** | 예 | 예 — product client 가 세션·영상 |
| 〃 | --prelogin worker | 같은 인자(`:317`) | 없음 — client 0 | — | client 0 | 제품 — 수행 | 예(생성자는 첫 줄 근처) | 아니오 — client 0 |
| 〃 | --old-host(baseline) | — | — | **NOT RUN, 미검증**(Finding-3 ④) | — | baseline 제품 — 수행 | (과거 실행: 예) | (과거 실행: 세션 case 에서 예) |
| host_punch_reply_e2e | armed·move·ack·badtoken·expiry·late | 같은 인자(`:290`) | StartHost `:361`, adopt `:342` | INVALID, 연결 0 | 없음(Hello·control) | 제품 — 수행 | 예 | 세션이 성립한 case 만 가능(정적) |
| 〃 | prelogin | 같은 인자 | 없음 | — | client 0(worker exit 14) | 제품 — 수행 | 예 | 아니오 |
| 〃 | REMOTE60_E2E_OLD_BIN | — | — | **NOT RUN**(backstop) | — | — | — | — |
| host_migration_e2e | carried·unlisted·rejected·down | 같은 인자(`:172`) | StartHost `:300`, adopt `:278` | INVALID | client 0(등록만) | MigrationTest — **수행**(record-only 아님) | 예 | **아니오 — client 0** |
| host_reboot_delegate_e2e | 1 host | 같은 인자(`:186`) | `:410`, adopt `:219` | INVALID | 재부팅 요청(입력 아님) | MigrationTest(GNLinkStream.exe 로 staging) — 수행 | 예 | 가능 — 세션 |
| android_session_reboot_e2e | 1 host | 같은 인자(`:197`) | `:230`, adopt `:218` | INVALID | 재부팅 요청 | 제품 — 수행 | 예 | 가능 — phone 세션 |
| bwe_observe_e2e | 1 host(자기 창 capture) | 같은 인자(`:393`) | `:429`, adopt `:423` | INVALID | 없음 | 제품 — 수행 | 예 | 예 — 영상 240 s |
| host_control_deadline_e2e | 1 host | 같은 인자(`:224`) | `:267`(staging host.log), adopt `:262` | INVALID | 없음 | 제품 — 수행 | 예 | 가능 — 원시 소켓 연결이 capture 를 시작하는 경우 |
| host_two_dispatcher_e2e | 1 host | 같은 인자(`:252`) | `:293`(staging host.log), adopt `:287` | INVALID | 없음 | 제품 — 수행 | 예 | 가능 — 세션 |
| host_fatal_exception | run(): std·winrt(startup fault) | 같은 인자 + 127.0.0.1(`:199`) | 없음 — 이 시험 소켓 0. power off 줄은 사후 기록 | — | 0 | MigrationTest — 수행 | 예(생성자는 throw 전) | **아니오 — configure 단계에서 throw** |
| 〃 | run(): none(정상 control) | 같은 인자 + 127.0.0.1 | 사후 기록(입력·power 두 줄) | — | 0 | MigrationTest — 수행 | 예 | **아니오 — client 0** |
| 〃 | run_with_client(): probe-trap·wrapper-trap·wgc-throw·wgc-false | 같은 인자 + 127.0.0.1(`:253`) | Hello 마다 직전 `:288` | case 종료, Hello 0, INVALID 계수 | bare Hello | MigrationTest — 수행 | 예 | 아니오 — capture 전 종료(정적) |
| 〃 | run_with_client(): normal | 같은 인자 + 127.0.0.1 | Hello 마다 직전 `:288` | 〃 | bare Hello | MigrationTest — 수행 | 예 | 가능 — encoder 줄까지 감 |
| 〃 | --prelogin worker(log root) | 같은 인자 + seam 127.0.0.1(`:395`) | 사후 기록(power 줄) | — | client 0 | MigrationTest — 수행 | 예 | 아니오 — 첫 단계에서 throw |
| os_input_record_e2e(신규) | record-only Host | 인자 없음(power ON 이지만 기록만) | `:193` record-only 줄, adopt `:184` | INVALID | Tab·move·click·text → 기록만 | **InputRecordTest — 수행 안 함(기록만)** | 기록만(수행 0) | 기록만(`ctx none`, 수행 0) |
| control_resume, viewer_mouse_xbutton, viewer_paste, viewer_release_all | — | — | — | **기본 REFUSED exit 3**(Host 이전) | — | — | 실행 안 함 | 실행 안 함 |
| viewer_prelogin_login | — | — | — | 실행 금지(무변경) | — | — | — | — |
'''
s = s[:start] + table + s[end:]
open(p, 'w', encoding='utf-8').write(s)
print('ok')
