# APK inbox — A/B r3 결과 검토 / 역순1회

2026-10-30. **기존 A FAIL·B 단독 관측을 수용하고, 같은 조건의 역순 B→A batch1회를 추가 허용한다.** APK0.2.33 출하 보류는 유지한다. 이번 결과는 아직 동일120초 matched A/B 비교가 아니다.

## 고정/직접 확인

정본 `result_ab_shoff_r3.md` SHA-256 `076da46bcaea254d7c629f84ee176a5ae7b21114a8490d8eb6b257bfacc861bf`.

Codex는 다음 원본 fixture_result/result.json/Host log **6개 hash**를 문서의 전체 값과 직접 대조했다.

- A `ab-a-shoff-20261030014717-45932`: a1b4a098… /4d0d49f0… /40cf83cf… 일치.
- B `ab-b-shoff-20261030015253-2608`: d245e390… /d84882684… /d092d212… 일치.

양쪽 fixture의 node end/a exit0·failures none, Host input none/power off, slot의 job residual0/forced0/ended_confirmed1 및 실제 arm rc를 확인했다. A는 fixture0/AVD1로 기능 FAIL이며 INVALID로 바꾸지 않는다. B는 fixture0/AVD0으로 실행 PASS다.

B-only script SHA-256 `75fb1cd1488aacd06e488833ec043173a56b7e5276874693539b0ba52fca5e6d`와 reviewed r3를 직접 diff했다. A 항목 두 줄 삭제·주석 추가뿐이고, 보존된 A 정리 뒤 B1회를 진행했으므로 **앞 승인 “안전 확인 뒤 B만 독립1회” 범위에 맞는다.** 별도 source go를 안 받은 위반으로 처리하지 않는다. 문서의 “이 파일 검토받지 않음”과 기존 실행 권한을 구분한다.

## 결과 수용 범위

- A: watched4969ms·peer_lost의 **FAIL**을 보존한다.120초를 채웠다고 환산하지 않는다.
- B: watched120398ms·8/8/18 NOT RUN의 **제한된 실행 관측**을 수용한다. 같은 고정 source에서 영상/control을 유지한 사례이지 전체 기능/품질·출하 EVIDENCE OK는 아니다.
- B Host 전체 keyframe343·keyframe-request339(unique339)를 원문에서 직접 집계했다. 세 구간 합보다 각2개 많은 것은 바깥 구간으로 따로 두면 된다. 모든 줄을 억지로 세 구간에 넣지 않는다.
- B의229 요청/약1.86회/s는 문서의 **watch SEGMENT epoch 구간 약123.007초** 기준이다. watched120398ms와 sample-end까지 포함한 SEGMENT 길이는 다르므로 분모를 혼용하지 않는다. 마지막 video_debug는 list 구간의 누적 값이므로 watch 전용 in/out·PTS 품질 수치로 사용하지 않는다. 새 run도 구간 경계/실제 snap 시각 차를 사용한다.

A가 조기 종료됐으므로 B의120초 요청량과 직접 회귀 배율을 계산하지 않는 정리는 맞다. B에서 trace-off로도 요청 반복이 관측됐다는 사실은 유지한다.

## A의 c2h와 peer_lost 해석

같은 relay flow7fbd9774의 raw snapshot을 직접 확인했다.

-16:50:09.670Z의 c2hRx age2724ms.
-16:50:14.670Z의 c2hRx/c2hSent age10ms.
-16:50:19.671Z의 c2hRx/c2hSent age1275ms.

Host raw peer_lost는 KST01:50:17.775다. clock을 맞추면 relay가 그 뒤에도 c2h datagram을 받고 socket send callback 성공을 기록한 것은 수용한다. 그러나 **어떤 control stream/message/fragment의 ACK가 Host에 도착·인증·채택됐는지는 이 합계로 알 수 없다.** c2h는 다른 control data/NACK/다른 packet일 수 있고, socket send 성공은 Host 수신의 증거도 아니다. 이미 이전에 쌓인 unacked head의 예산이 소진되는 것과 UDP 흐름 재개는 양립한다.

따라서 “c2h가 흐르는 중 Host peer_lost”는 유용한 시간 정보지만 relay/UDP/Host 검사 무관 또는 A APK 고유 root cause의 확정은 아니다. 기존 close bucket의 kind별 흐름을 추가 offline 분석할 수 있으며, payload/secret을 새로 수집할 필요는 없다. 역순 비교에 새 제품/계측을 섞어 원인을 한 번에 바꾸지는 않는다.

## 역순 B→A 추가1회 승인

제안의 목적은 타당하다. 첫 arm/codec warm-up·실행 순서 가능성을 가르고 B의 요청 반복 표본을 얻는다. 다만 Android boot_id가 같았으므로 A를 “새 Android 부팅 직후”로 부르지 않는다. A가 또 실패하고 B가 성공하면 **이 조건의 A 경로와의 연관성이 강해지는 것**이며 환경과의 상호작용/실패 원인까지 분리한 보편적 증명은 아니다.

검토된 reverse wrapper를 준비했다.

- [ab_shoff_batch_reverse_r4.ps1](D:/remote/remote/.claude/codex_decoder_liveness_review/ab_shoff_batch_reverse_r4.ps1)
- SHA-256 `4b3f274c510c786d43c630e23048c5470856c2d6ed01485aeb0c2ef46da36df9`.
- reviewed r3의 exact hash에서 순서 재배열1줄·설명 주석만 추가했다. 모든 pin/boot 검증·첫 nonzero/unknown rc 중단·정리 규칙은 유지한다. PowerShell parse0 오류, 선언부만 순수 평가해 실제 arm 순서 **ab-b-shoff, ab-a-shoff**를 확인했다. 실제 SDK/기기는 실행하지 않았다.

검증용은 이 파일을 쓰거나 같은 순서 변경만 적용하고 hash를 기록한다. **이 구체 변경은 이번 승인에 포함하며 다시 go를 받지 않는다.**

조건:

1. 같은 A8c6422fe/B32bb788f plain·r12 snapshot·server372e6eb8·Host/harness/job·SH-off/effective direct·120초 설정을 실행 전 다시 대조한다. C prototype/별도 trace APK를 넣지 않는다.
2. 원문 tag가 같아도 slot script의 시각/pid로 생성되는 **새 run 폴더**를 쓰고 이전 자료를 덮지 않는다. 전후 idle/실행 중 부하·boot/listener·first frame/관찰 지연/실제 구간과 누락을 기록한다. 사용자 앱/다른 adb를 종료하거나 환경을 맞춘다고 정상 데이터를 제외하지 않는다.
3. B가 첫 arm에서 실패하면 reviewed 중단 규칙대로 A는 NOT RUN이다. 이 **추가 batch1회 뒤 즉시 A/B를 또 반복하지 않는다.** 원문과 정리/실제 watched 길이를 보고 다음 판정을 잇는다.
4. 기능 FAIL·정리 실패·INVALID를 구분하고, 무인증 update 예외/Host OFF·child EOF·job·repo 경계·APK 초기화/clipboard 미조작 등 기존 허용 조건을 유지한다. Source가 다른 old-apk lane의 결과를 이 arm에 옮기지 않는다.
5. PC old-apk-lane과 directory-retry 등 기존 필수 작업의 완료/슬롯 소유를 검증용이 확인하고 **그 뒤** 배정한다. 현재 사용 중인 슬롯/프로세스를 중단하거나 이미 있는 slot 파일을 지우지 않는다.

관찰 뒤 두 arm이 같은 길이로 성공하면 그 pair 안에서 요청/IDR·UDP bytes·fps/gap·CPU/PSS/queue 참고치를 비교한다. A가 또 조기 종료하면 stability 차이와 B 반복 관측까지만 보고하며 성립하지 않은120초 비교를 만들어내지 않는다. control-only 항목과 inbox 정책 C의 결정·검증은 각각의 근거로 잇는다.

이번 Codex는 원본/hash와 배치 diff를 읽고 검토용 reverse script·순수 parse/order 검증만 수행했다. 실제 Host/Viewer/AVD/adb·GPU/OS 입력·설치·운영·삭제·제품 수정·commit/push는 없다. 미추적 검토 산출물만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
