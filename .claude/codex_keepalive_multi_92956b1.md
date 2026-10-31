# keepalive-multi r2 — 92956b1

2026-10-16 · 기준 2c22313 → cf82498 + 카드 표시 92956b1.
**SOURCE OK — M1/M2/L1 종결. 통합 준비 가능. 게시 판단은 정책값 사용자 답과 아래 클립보드 결합 근거가 남아 보류.** 추가 제품 보수 라운드를 요구하는 소스 결함은 이번 delta에서 확인하지 못했다.

Codex 직접 확인: r2 제품 diff, SessionTable::Commit, native Established/Generation/JNI, MainActivity tryCommit/cancelConnecting/endEverything/onStop, ResumeFromPark·클립보드 UI 경로, 보고/검증용 독립 결과. 빌드·기기·AVD를 직접 실행하지 않았다.

## 종결된 지적

- **M1:** native Commit은 entry가 Connecting 예약인지 확인하고 요청 generation 대조 후 Established=0이면 Pending으로 아무 축출도 하지 않는다. Established<0이면 자기 예약만 Fail, Connected일 때 registry Commit/LRU로 간다. Kotlin pendingCommit은 op/global epoch를 재확인하고 20s 유계 tick으로 기다리며 표시/활성화도 commit 뒤에 한다. WorkerMain의 Connected 대입 경로와 Connect의 worker-start 반환 차이를 확인했다. 기존 실패 유발 경계가 닫혔다. 현재 호출부는 madeGeneration!=0인 연결만 pending에 넣으며, native API의 generation=0 예외를 ‘임의 세대 안전’으로 일반화하지 않는다.
- **M2:** directoryBusy의 조용한 return이 제거됐다. 다른 PC 탭은 connecting entry만 invalidate/Fail하고 최신 탭을 진행하며, 같은 PC 중복 탭은 같은 intent로 남긴다. stale worker의 후속 UI/채택은 handle/op/epoch 검사로 막는다. 관계없는 parked entry를 취소하지 않는 방향을 확인했다.
- **L1:** 실제 onStop에서 configuration 예외를 제외하고 endEverything을 호출한다. gesture/입력 release·키보드·selection/decoder 정리 후 active/parked/connecting 전부 종료·epoch 무효화, UI 초기 상태로 이어진다. network callback과 모두 끊기도 전체 종료로 통일됐다. 화면 하나만 있을 때 network callback을 별도로 등록하지 않는 범위는 문서대로 제한하며, 모든 경로 변화를 능동 감지한다고 쓰지 않는다.
- run_detached의 moved-from fallback 위험은 shared task 보유로 보수됐다. thread 생성 실패 시 동기 fallback은 transition 락 아래 수행될 수 있어 빠른 전환 보장은 예외라는 기록을 유지한다. 실패 상황까지 항상 비차단이라고 쓰지 않는다.

## 실제 근거의 평가

검증용 독립 고정 92956b1의 Android213/0, session_table 등 반복 통과, 실제 앱 후보35/35 대 r1 동일 순서21/35에서 M1/M2 실패 재현을 수용한다. P8 Hello 실패에 축출0·A/B/C 보존, P9 실제 연결 성립 뒤 A만 LRU, P3/P4 지연 응답 stale, P11/P13 전체 종료는 요구된 행동 근거다.

45s 시험 정책의 독립15/15는 각 PC TTL·재활성화의 새 기한·새 handle·기존 active 보존을 입증한다. 실제 5분 만료를 이번에 재실행한 것으로 표기하지 않는다. 기존 TTL 상태기계와 동일 정책 주입 경로 및 출하값 분리로 수용한다. 92956b1의 카드 문구 변경은 남은 시간 표시만이며 ttl 의미를 바꾸지 않는다.

구 host pause 미지원의 자기 entry 종료·다른 연결 보존과 relay의 다른 기기 대체 뒤 자동 재탈취0도 수용한다. ‘direct’는 LAN private 후보+시험 forwarder 경로이며, 회사 NAT/WAN 직접 연결 성공까지 뜻하지 않는다. 크게보기 reset 및 6M이 active host로만 적용되는 P7는 결합 근거다.

출하형 assembleDebug 해시 5e30e30d3441d0195726940dbe18d1e3152a60c375d46280068122d3f532a943 및 시험 정책/주소 구분은 독립 보고 근거다. hook 이름 리터럴과 testHook 실행 기전을 구분한 기록을 유지한다.

## 남은 출하 조건 — 새 제품 설계가 아니라 검증 잔여

1. **정책값:** 최대3대/각5분은 아직 사용자 확인 중이다. 기존 질문의 답을 검증용이 수렴한 뒤 정책 객체/UI/한도 기대값을 함께 고정한다. 이 소스 OK가 기본값 승인이나 게시 허가는 아니다. 값이 달라지면 영향 정책·한도 시험만 맞추며 무관한 전체 회귀를 반복하지 않는다.
2. **클립보드 결합:** 이번 6-host fixture는 clipboard를 꺼 두었으므로 기능 누출0을 입증하지 못한다. 실제 사용자 PC/호스트 클립보드를 건드리지 않는 **기존 격리 station/no-capture parkclip + 모의 로컬 clipboard sink** 또는 동등한 제품 controller/entry/binding 결합 시험으로 한 묶음 확인한다. A의 수신 텍스트를 보류→A park/B active→늦은 A 수신/정리 해제→B 또는 로컬 적용0, B의 새 복사는 B만, A 복귀 뒤 park 중 텍스트/옛 pending push 재생0과 새 복사 정상 전달을 함께 단언한다. 이미 있는 단위 state reset을 다시 세는 것으로 대체하지 않는다. 제품 클립보드 handler를 통과하고 적용 대상/실제 sink 호출까지 관측하면 실제 사용자 clipboard나 물리 폰은 필요 없다. 실패하면 그 범위만 수정한다.

이 둘이 충족되면 새 제품 delta/새 실패가 없는 한 **통합 영향 회귀→버전·서명·게시·외부 확인**으로 진행할 수 있으며 전체 소스 재승인 왕복은 필요 없다. 게시 실행 권한은 현재 사용자 지시 범위를 따른다.

## 나머지 미검증 처리

- 1088 padded crop의 실제 폰 표시, 제조사 MediaCodec·실 회전·Wi-Fi/LTE 전환·배터리는 실기 항목으로 남긴다. 새 token binding의 두 PC reset/통계 분리는 독립 decoder_binding 시험 근거가 있어 이 항목 때문에 다시 전량 AVD를 요구하지 않는다. 실제 1088 효과 성공이라고 미리 말하지 않는다.
- 메모리/스레드 비용 측정은 에뮬레이터 전체 네트워크·상태별 샘플이며 첫 기준선의 배경 통신 교란을 인정한다. 3대45thread→종료39는 일부 자원 해제 근거일 뿐 무누수 증명이 아니다. 장시간 무누수·실폰 배터리 ‘검증 완료’ 문구는 금지한다. 명확한 증가/회수가 안 되는 누수가 새로 발견되지 않는 한 현재 표본 한계 자체를 추가 차단 사유로 만들지는 않는다.
- 불필요한 같은 이름/동일 host 재연결과 late binding의 실제 동작은 고정 후보의 안전한 정책 범위 내에서 계속 계측할 수 있으나 현재 닫힌 M1/M2/L1을 다시 여는 별도 요구는 아니다.

최종 통합/게시 패킷에는 source commit·버전/code·인증서·실제 게시 APK hash·manifest pair·PC payload 불변·직접/인용/실기 잔여를 연결한다. 기존0.2.30 대비 onStop active 종료 변경을 사용자에게 분명히 안내한다. 이번 검토 파일 외 제품·설치본·commit/push 변경은 하지 않았다.
