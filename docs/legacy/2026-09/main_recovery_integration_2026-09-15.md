# main → Host/PC 복구 브랜치 통합 — 2026-09-15

- 대상: `audit/host-pc-recovery`, 기존 HEAD `bb86e98`.
- 가져온 main: `437c87cc9ff70104b72f1eeb89ac525b2c33905f`, 제품 소스 버전0.2.126. 공통 조상035c4a4.
- 백업: `backup/host-pc-recovery-before-main-20260915` → bb86e98. 더 이전 Native 원본daacbb2 백업도 유지.
- 사용자 승인 범위: main을 현재 분리 브랜치로 통합하고 충돌·중복을 정리. main으로 역머지·push·NAS 게시·설치/재시작은 하지 않는다.
- 이력은 squash하지 않는다. 기존12개 Native 수정 커밋과 main의 개별 커밋을 두 부모를 가진 merge commit으로 연결한다.

## 충돌·중복 처리

명시적 충돌14파일(제품/테스트10, 문서4)을 해결하고 자동 병합된 파일도 대조했다.

| 영역 | 통합 결과 |
|---|---|
| 마우스 해제 | main의 `release_forwarded_button`으로 통일. 동일 목적의 옛 inline 구현과 중복3개 시험 제거. 시험 창 PID/owner 제한은 보존 |
| UDP 수신 | main의 `viewer_udp_ingress.hpp` 하나로 통일, `udp_receive_pump.hpp` 삭제. main의 오래된 datagram 폐기·overflow→discontinuity/IDR·ingressDrops 계측을 유지. 소유 worker의3초 종료 제한과 예외 시 consumer 깨우기는 추가 보강 |
| DXGI 정책 | main의 첫 code44 이후 supervisor 실행 동안 WGC 유지. 옛2회/5분 만료 정책 폐기. `HostRecoveryPolicy`는 이 정책의 단일 구현으로 사용. Job Object/종료 제한 보존 |
| backend 환경 | 정상 실행은 부모의 전체 환경 그대로 복사(미설정·긴 값도 보존). 격리 중 자식 환경에만 WGC 지정. 부모 프로세스의 임시 backend 변경·복원 경합을 제거 |
| 프로토콜 | 이미 사용된 peer version bit0x40 유지, frame heartbeat는0x80으로 배정. 중복 금지 static_assert 추가. 기존 version request/response와 Pong layout 유지 |
| 입력 큐 | main의 `WaitForInput`·notify·입력 우선8개 burst/배경 작업 공정성을 유지하고 복구 브랜치의 큐 상한·release 보존·오래된 비해제 입력 폐기를 결합 |
| 인코더/캡처 | main의 원본 content 크기 보존·최종 변경 픽셀 전달·FPS 기반 kick 지연 보존. 실패 target/캡처 pending 재시도도 유지하며 새 복구 경로에 activeFps 전달 |
| liveness/표시 | main의5초 decode/publish wedge 판정과 새 output/UI/control 감시를 결합. main의 진단·클록 구분 유지. GDI 실제 성공 검사와 표시 실패 복구 보존 |
| Client | worker 소유·owner epoch·계정별 log upload와 main의 viewer PID/host/version 진단 문맥을 결합. 모든 Viewer 종료가 live count에 반영되며, UI 채택 때 최신 count를 읽는다. 오래된 Viewer는 새 reconnect를 교체하거나 예산을 초기화하지 못함 |
| 문서 | 최신 main 사건 문서를 정본으로 사용. 양쪽 history/계획 내용 보존. main history의 비어 있지 않은 줄 누락0 및 UTF-8 BOM 보존 확인 |

자동 재연결은 기존 복구 브랜치와 같이 가장 최근 선택한 연결에 대한 제한적 재시도다. 모든 병렬 Viewer의 독립 자동 재연결이나 직전 개별 창 복원까지 구현한 것은 아니다.

## 자체 검증

환경: 콘솔session1 Active, 단일 모니터, MSVC x64 Release. 별도 test account·profile·loopback endpoint 사용. 사용자 설치 제품 무접촉.

- 제품4종(Host/Stream/Viewer/Client) 및 관련 시험 타깃 빌드 성공. 이후 변경 없는 최종 증분 빌드로 제품/주요 통합 시험 타깃 재확인. 로그 `.claude/main-merge-build.log`, `main-merge-final-build.log`.
- Native13종 첫 실행:12종 exit0, `remote60_host_encode_epoch_test` exit1. 결과와 실행 파일SHA256는 `.claude/main-merge-results.json`에 그대로 보존.
- 통과12종: recovery process, UDP ingress, peer version, shared core, viewer liveness, 실제 WndProc, Host runtime failure, final capture update, kick, ABR, frame gate, UDP recovery. UDP ingress는 실제 두 소켓과 tick 예외 종료를 검사. recovery process는 같은 통합 ingress에 실제 control packet을 전달하고 video backlog 중 ACK 처리를 확인.
- 인코더 첫 실패: HW MFT의 synthetic kick 관측 시험에서 출력이 입력 timestamp 기준800,000us 뒤에 드러나150,000us 기대를 만족하지 못함. 이는 실제 벽시계 지연 측정값이 아니다. 해당 시험/codec/epoch gate 소스는 양 부모에서 동일. 다른 시험 종료 후 **동일 실행 파일** 1회 재실행은 exit0(해당 프레임이 kick 전 이미 출력됨). `.claude/main-merge-encoder-recheck.log`. 최초 실패를 PASS로 바꾸지 않으며, 정확한 비결정성 원인은 미확정이다.
- 실제 제품 Shell/native/HTTP UI 시험 PASS. 로그인 A→지연 응답→로그아웃→B 로그인, 전용 WebView browser crash 후 B 복원, 취소 버튼 native 전달, 설정 화면 busy 해제 검사. 추가로 오래된 Viewer 종료의 잔여 count 표시/해제와 새 reconnect 보존을 **실제 UI 결과 채택 함수**로 검사. 이3항목은 종료 결과를 주입한 것으로 실제 병렬 Viewer 프로세스 종단 시험을 대신하지 않는다.
- `.claude/client-recovery-ui.png`를 직접 열어 복구된 계정B 화면을 확인. 로그 `.claude/main-merge-ui.log`.
- 서버 전체 회귀 `node apps/directory/test/run.js` ALL PASS/exit0. durable 저장 실패/인증·relay·LAN 회귀 포함. `.claude/main-merge-directory.log`.
- 충돌 marker·staged whitespace 오류 없음. main의 kick/ABR/content metadata/version/update-effects 파일이 원본 main과 같은 내용임을 별도 대조.

## 남은 경계

통합·자체 검증 후보이며 배포 승인이나 전체 장애 해결 판정이 아니다. 독립 검토, 실제 GPU 반복 정체·다중 모니터·다중 Viewer 종단·WAN/게임/장시간 soak·UAC/설치는 남아 있다. HW MFT 최초 실패도 안정성 확인 대상으로 남긴다. main의1초대 순간 대기와 REL-V01 배포 바이너리 버전 표찰 문제를 이번 merge로 해결했다고 주장하지 않는다. 새 릴리스 manifest pin/서명/게시 검증은 하지 않았다.
