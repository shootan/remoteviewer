# KA ② e r3 — SOURCE와 검증 범위 결정

2026-10-28 · **SOURCE OK: feat 1aecc4851cdbe8676dcd90cbe3a91610894aa9d4 + 공용 0fb4676b618718c4cac02bfe0840fb3b122352b1. 이전 R1~R3와 D1 보수를 수용한다.** 실제 APK lifecycle/clipboard 행동의 EVIDENCE 및 출하 승인은 아니다.

결정 ①은 **현재 (c), 실 clipboard 미검증 유지**다. 결정 ②는 **시험 seam 빌드에 한정한 activation 실패 fault 추가 수용**이다. AVD 재실행은 해당 하네스의 공용 guard 통합·현재 안전 옵션을 지원하는 pinned Host 재빌드·시험 SOURCE 확인 뒤다.

## 고정과 검토

- report_e_r3.md SHA-256 1cc0628c1e6f753e4bd5b91eb1be0661add786d06faa88ad06d0f891bfd7ef48 일치.
- feat 7a0d577..1aecc48의 R1~R3/D1 Kotlin·JNI와 공용 0fb4676의 lease/runner를 읽었다. 중간 merge의 Host punch 큐 수명·시험 root 보수는 기존 판정을 유지한다. diff --check 통과.
- feat 1aecc48과 공용 0fb4676의 standby_lease.hpp / standby_runner.hpp가 각각 같은 blob인 것을 확인했다. 이후 1aecc48..7f07f1a는 별도 SOURCE OK인 X 경계 5파일뿐임도 확인했다. 이번 판정은 그 X 범위를 다시 여는 것이 아니다.

## R1 — 늦은 목록 복귀 완료의 재생성 보수 수용

forgetKept는 해당 handle의 listReturnEnds를 지우고, endAllKept는 남은 완료 항목을 모두 지운다. 단일 UI thread에서 listReturnTick/finishListReturn을 처리하므로, 끊기·owner 종료 뒤 map에 남은 완료를 다음 tick이 꺼내 lease를 만들던 원 경로가 사라진다. 정상 no_fence의 즉시 완료는 유지한다.

startStandby 자체도 화면 종료, 현재 로그인 account/origin 및 suppression을 검사한다. kept 카드 끊기도 해당 PC를 suppress한다. 다른 PC 탭만으로 정상 첫 전환을 취소하는 변경은 아니다. 새 owner·로그아웃/계정 전환·HOME 및 첫 전환 도중 끊기의 실제 UI 결과는 별도 확인해야 하며, E7h(a–d)는 미실행으로 유지한다.

## R2 — Activity별 lease 소유 보수 수용

process 내 AtomicLong에서 나온 구분자 o<n>|가 Activity별 native key에 붙는다. Start/Status/Observe/Provide/Release/ConnectStandby의 key가 standbyKey를 거치는 것을 대조했다. owner 1과 10은 구분자 때문에 prefix가 겹치지 않는다.

화면의 queued cleanup은 ReleaseOwner(prefix) → ReleaseWhere로 자기 slot만 map에서 떼어내고, 밖에서 Stop/join/release한다. 새 Activity가 같은 PC를 준비해도 다른 key이므로 옛 cleanup이 새 lease를 제거하지 않는다. Book incarnation/ifCurrent의 기존 capability 유효성 검사는 그대로이며, 낡은 작업의 endStaging도 현재 incarnation을 바꾸지 않는다. UI에서 HTTP 종료를 기다리는 설계로 바뀌지 않았다.

문구는 정확히 구분한다. **Activity 호출 경로에서 전역 ReleaseAll을 제거했다**는 뜻이다. JNI nativeStandbyReleaseAll export와 Kotlin external 선언 자체는 아직 남아 있고, runner 소멸자도 ReleaseAll을 호출한다. 이번 SOURCE 수용에 미사용 export 삭제를 추가 요구하지 않지만 “소멸자 외 소스에 ReleaseAll이 전혀 없다”는 설명은 맞지 않는다.

## R3 — 복귀 의도와 transport 분리 수용

PendingCommit.preparedReturn을 실제 commit까지 전달한다. activation 성공은 true, mine인 activation 실패의 같은 탭 ordinary RETRY도 true, 준비되지 않은 기존 lease를 놓는 cold fallback도 true다. tryCommit은 StandbyKeep.isReturn 한 곳의 결과로 read/Seed/Hold를 정한다. pathKind가 relay/private로 바뀌었다는 이유만으로 Fresh로 돌아가지 않는다. 사용자의 별도 cold 접속은 기존 Fresh 계약을 유지한다.

이것은 source 경로의 보수 수용이다. helper 단위·변이만으로 실제 clipboard 변경 알림, binding 교체, JNI baseline, 전송 wire에서 자동 push 0을 모두 입증했다고 하지 않는다. 실제 복귀와 실패 fallback에서 기존 text 자동 전송 0, 이후 새 copy 1, unreadable Hold, 다른 PC binding 오염 0이 남는다.

## D1 — 계측 의미 보수 수용

lease는 target tuple에서 온 Punch를 별도로 punches에 세고 stray/tail에서 제외한다. JNI 8번째 값과 Kotlin의 길이 확인 parser·로그까지 연결됐다. strays는 standby/HelloAck/Punch가 아닌 target datagram 계수이며, 종류별 영상/제어 계수 또는 grant 종료 증거가 아니다. r2의 “잔류 영상 tail” 해석 철회와 relay에서는 Punch가 전달되지 않아 punches=0일 수 있다는 구분도 수용한다. ke_r20의 25 배수 strays의 정체는 이번 변경으로 확정되지 않는다.

## Codex 직접 구성 단위 확인

0fb4676의 standby_lease_test와 의존 header 16파일을 별도 동결해 빌드·실행했다. 메모리 generation store와 자기 loopback fake peer만 사용한다. **rc 0, 75 PASS, RESULT PASS.**

직접 확인한 새 행동:

```
5 Punches: punches=5, strays=0, tail=0
ReleaseWhere("o1|"): gone=2, size=2, host releases=2
o2와 o10의 lease는 parked 유지
옛 owner key의 late Provide는 거부
빈 prefix는 회수 0
```

- 산출물: .claude/codex_ka_e_review/r3_0fb/lease_0fb.exe, build_fixed.log, run.log 및 동결 소스.
- exe SHA-256: 8927ead466cf76a40d6ad45e4e99be678af0a63f3e8f610791f049a44bcab2b0
- 최초 수동 build는 시험용 REMOTE60_STANDBY_TEST_HOOKS 정의를 빠뜨려 compile 실패했다. 고정 CMake target의 정의를 적용한 재빌드가 위 유효 결과다. 제품 compile 실패로 분류하지 않는다.
- Kotlin 313/0·변이와 기존 AVD 수치는 작업용 보고다. Codex가 APK/AVD/JNI 또는 실 clipboard를 실행한 것은 아니다.

## 결정 ① — 현재 실 clipboard 미검증 유지

공유 차단을 확인하지 못한 emulator-5554의 clipboard를 읽거나 쓰거나, 공유 여부를 알아보려고 사용자 Windows clipboard에 sentinel을 넣지 않는다. 별도 emulator라는 사실만으로도 격리가 증명되지는 않는다. 공식 문서가 설명하는 -no-window의 의미는 그래픽 창 표시 비활성화이며, 그것을 clipboard 공유 차단 근거로 쓰지 않는다. [Android Emulator 명령줄 문서](https://developer.android.com/studio/run/emulator-commandline).

이번 packet은 (c)로 기록한다. 이는 R3 위험이 해소됐다는 출하 waiver가 아니다. 후속으로 **사용자 clipboard와 연결되지 않는 것이 확인된 별도 emulator/격리 실행 환경**이 준비되면 (a) 방식으로 검증할 수 있다. 그때도 Host 쪽 실제 clipboard 효과는 검수된 private window station 등 자기 격리 대상으로 한정한다. 당장 사용자 실기기를 요청하거나 현재 공용 emulator 설정을 임의 변경할 필요는 없다.

그동안 record/provider 대역으로 실제 commit→Seed/Hold→송신 판단을 검사하는 것은 유용하지만, 그 결과를 Android/Windows 실제 clipboard 검증으로 부르지 않는다. 전체 task 완료나 최종 출하 판단에서는 이 남은 항목을 따로 표시한다.

## 결정 ② — fallback용 한정 fault 수용

기존 시험 Host seam 빌드에 **해당 시험의 activation 실패 episode만 결정적으로 만드는 fault**를 추가해도 된다. 제품의 정상 activation 계약이나 wire 규격을 바꾸지 않는다. 다음 결과가 확인 가능해야 한다.

- 시험이 명시적으로 켠 해당 lease/activation에만 적용되고 적용 횟수·대상이 로그로 식별된다. 전체 Host 또는 뒤의 ordinary cold connect를 계속 망가뜨리는 fault가 아니다.
- 실제 실패가 nativeConnectStandby의 gen=0, mine=true를 거쳐 **같은 탭의 ordinary fallback 성공**으로 이어진다. UI handler를 직접 호출하거나 성공 return 값을 대역으로 바꾼 결과만으로 이 경로를 입증하지 않는다.
- fault는 시험 build에만 존재하며 정상 출하물의 활성화 방법/marker가 없어야 한다. 해당 fault가 없는 대조와 같은 입력 순서에서 실제 activation 성공이 확인된다.
- fallback 후 preparedReturn/baseline 동작 검사는 위 clipboard 격리 상태에 맞춰 source/provider 증거와 실 clipboard 증거를 구분한다.

세부 fault 위치·기존 상태 코드/timeout 사용은 작업용이 기존 Host seam과 맞춰 구현하고, **guard 통합·pinned Host 재빌드와 한 고정 시험 후보로 SOURCE**를 올린다. 이 설계 수용은 미검수 fault/하네스를 바로 실행하라는 승인이 아니다.

## 다음 단계와 과거 실행 처리

검증용은 기존 작업용1 e task 한 정본에 guard 통합·현재 pinned Host·fallback fault·남은 UI 재현을 취합한다. 입력 none과 power OFF의 launch별 두 줄 gate, 실행 중 감시와 종료/EOF/final 판정은 이미 검수된 공용 guard를 사용하고 재구현하지 않는다. 전원 옵션 이전 old/pinned 바이너리로는 재실행하지 않는다. old-host는 NOT RUN으로 유지한다. 금지된 viewer_prelogin_login_e2e는 이 승인 범위에 없다.

ke_r20/r21 및 kae_e3는 정책 이전의 관찰로 분리한다. ke_r20/21은 input none 로그가 있었지만 power/gate는 없었고, kae_e3의 Host는 기본 input ON이었다는 보고를 유지한다. 실제 OS 효과 발생 여부의 미확인을 0으로 바꾸지 않는다. E7h와 Activity 교체 gate 시험, raw-IP+AUTO, prelogin 실제 UI, 실 NAT/비용/p95도 완료 처리하지 않는다.

**ke_r21 E7e peer_lost는 원인 미확정·간헐 가능 항목으로 남긴다.** 새 안전 하네스에서 양방향 제어/relay 관측을 붙여 조사하고, 한 번 통과한 사실만으로 닫지 않는다. SOURCE OK가 이 동작 관찰을 해소한 것은 아니다.

이번에는 검토 문서와 자기 구성 단위 산출물만 만들었다. 제품 변경·NDK/전체 제품 build·AVD/실 Host/Viewer 실행·clipboard 조작·설치·운영·삭제·commit/push는 없다. 기존 구현/안전 기록의 history·계획·커밋은 검증용이 취합한다. 사용자 현재 조치 없음.
