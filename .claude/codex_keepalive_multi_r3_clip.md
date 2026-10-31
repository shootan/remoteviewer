# keepalive-multi r3 — 클립보드 결합 조건 종결

2026-10-16 · 시험 a2e9686 / 제품 92956b1.

**이견 없음. 92956b1 소스 OK에 남긴 클립보드 자동 검증 조건 충족. 정책값 사용자 답은 아직 별도 대기.**

직접 확인: 92956b1→a2e9686 diff는 시험 cpp 1개와 CMake 시험 target뿐(제품 delta0), 시험의 FindByBinding→제품 controller→모의 로컬 sink 경로·정상 copy/held 대조·stale binding 거절·park/resume·host 적용 길이 단언, 보고와 독립 r3 절. Codex는 실행하지 않았다. 독립 일반30/0×3·held15/0을 인용한다.

A의 보류 수신이 실재함을 held 대조로 확인하고, A park/B active 뒤 old A binding·B 경로에서 적용0, B 새 복사만 B에 도달, A 복귀 뒤 park 중 수신/옛 pending 재생0 및 새 복사 전달이 함께 확인된 구성이 앞선 요청에 부합한다. 두 host가 같은 격리 station clipboard를 공유하므로 수신 대상은 각각의 host 적용 로그·유일한 텍스트 길이로 판정한다는 제한을 유지한다. 서로 다른 실제 두 PC clipboard를 시험했다는 뜻은 아니다.

사용자 WinSta0 sequence 보존·no-capture host·양 host 생존 상태에서 판정한 시험 경계도 수용한다. JNI/Kotlin drain-push/Android ClipboardManager는 대체됐으므로 물리 폰까지 성공한 것으로 확대하지 않는다. 본 조건은 이미 모의 sink를 허용한 제품 entry/controller 결합 검증 요청이었으므로 이를 이유로 또 같은 시험을 요구하지 않는다.

제품 승인 92956b1에 변화 없음. 한도/TTL 사용자 답을 수렴한 뒤 기존 통합·릴리스 절차로 잇는다. 값이 그대로라면 제품 재승인 라운드는 필요 없다. 값이 바뀌면 정책/UI·관련 기대값의 영향 범위만 대조한다. 게시 권한은 현재 사용자 지시 범위를 따른다.

새 문서에는 onStop 미결정을 재등장시키지 않는다. r2에서 이미 전체 종료로 확정·구현·검증한 사항이며, 예전 r1 검증 절의 문구는 역사 기록이다. 현재 남은 정책 미결정은 연결수/TTL이다.

검토 기록만 작성. 제품·설치본·commit/push 변경 없음.
