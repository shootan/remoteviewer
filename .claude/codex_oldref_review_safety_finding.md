# Codex 검토 probe의 모니터 깨움 호출 1회

2026-10-30 09:32:58 +09:00 (power_spy.log 기록 시각)

구·신 참조 SOURCE 검토 중 Codex가 작성한 `.claude/codex_old_apk_review/oldref_r1_checks.py`의 power spy가 PostMessage alias만 가로채고 **직접 PostMessageW**를 빠뜨렸다.

old-ref-163 ff98f19의 HostPowerKeepalive를 실제 Host 프로세스가 아닌 작은 검토 exe에서 호출했다. OFF 경로는 spy exec/input/post0이었다. 이어 ON 부정 대조에서 exec4/input2는 spy로 기록됐지만 post0이었다. 고정 header의 wake helper는 PostMessageW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, -1)를 직접 호출하며, 해당 exe의 dumpbin /IMPORTS에도 PostMessageW가 있었다. 따라서 **깨움 broadcast1회가 실제로 나갔다.** ON에서는 streaming 시작을1번만 일으켰고 중복true는 재호출하지 않아1회로 특정된다.

실제 SendInput/SetThreadExecutionState import는 없었고 두 API의 호출은 spy로 기록됐다. 실제 마우스 입력/실행상태 요청은 이 probe에서 발생하지 않았다. 모니터가 실제 켜졌는지·사용자 화면에 보이는 영향은 미확인이다. Host/Viewer/AVD·제품 설치·서비스·배포 실행은 없었다. 검토 exe는 rc1로 끝나 더 실행되지 않았다.

원인은 Codex 검토 코드이며 worker의 안전 port 실패로 돌리지 않는다. 이 ON 실행을 안전 검증 PASS로 쓰지 않는다. diag 메모리 채널 시험과 source 비교의 귀속도 분리한다. 원본 cpp/exe/build/log/import 근거를 보존하고 삭제하지 않는다.

보수는 직접 W/A API도 가로채고, **실행 전 import 검사로 SendInput/PostMessageA/W/SetThreadExecutionState가 없음을 강제**하는 것이다. 그 검사가 실패하면 실행0으로 끝낸다. 보수 전 exe 재실행은 금지한다. 실제 사용자 상태를 되돌린다는 목적의 입력/전원 조작은 하지 않는다.

보수 후에는 dumpbin import gate 통과 뒤 실행했다. OFF exec/input/post0, ON은 spy exec4/input2/post1만 기록하고 rc0이었다. 실제 OS 효과 API import는 없었다. 이는 보수 전 사고를 지우는 결과가 아니다. 사고 cpp/exe/log/import는 `oldref_r1/power_spy_UNSAFE_original.*`로 따로 보존했고, 두 실행의 귀속과 hash는 같은 폴더 review_hashes.json에 기록했다.

검증용에게 이 사실을 별도 안전 통지하며, 기존 안전 ledger/history 기록에 사실·영향 미확인을 연결하도록 한다. 이번 사건이 이미 실행하지 않은 구/신 Host/AVD 대조를 무효화한 것은 아니지만, Codex ON spy 자체는 부적합 근거로 남긴다.
