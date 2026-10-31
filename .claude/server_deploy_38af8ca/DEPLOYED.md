# directory server deploy — relay-wire-ip (2026-10-11 12:43 KST, 검증용 remote#b9iz9dxq)
- commit main=origin/main=38af8ca (fe6b329 + 38af8ca, base 236b858). Codex SOURCE OK (codex_relay_wire_ip_38af8ca.md), 검증용 독립 suite ALL PASS.
- 파일 8개 /opt/gnlink/remote60-directory: server.js fdc9cbe8… relay_diag.js 0fe62917…(신규) 나머지 6개 236b858 과 동일(변경 없음). 서버 디스크 해시 == 로컬 staging == git LF blob.
- 서비스 remote60-directory (gnlink user unit): restart 12:43:18 KST, active, MainPID 3142968 → 2951580.
- 백업: /opt/gnlink/remote60-directory-backup-20261011T034305Z (relay_diag.js 없음 — 롤백 시 삭제; `bash .claude/deploy_relay_wire_ip.sh rollback`).
- 영향: 메모리 세션·관측·relay map 초기화 → 클라이언트 재로그인·재관측 필요. 클라/APK 변경 없음.
- 미검증(현장): Home PC → 회사 PC 새 connectId 의 punch→bound→active + 화면·입력, LTE 폰 경로 무회귀, 실제 relay ingress 가 192.168.0.1 인지.

## 현장 확인 (2026-10-11 15:01 KST)
- 사용자: 집 PC → 회사 PC 접속 성공 보고("회사로 접속 완료").
- journal connect=2ef608dd: `[relay] eligible client=175.207.45.151:40307 wire=192.168.0.1` → `punch from 192.168.0.1:40307; answering` → `bound client=192.168.0.1:40307 host=211.218.222.1:43000` → first c2h/h2c → `active; helloAck in 3ms`. 수정 전 모양(eligible 뒤 침묵)이 사라짐. 실제 relay ingress = 192.168.0.1 (관측과 동일, 계약 전제 충족).
- 미검증으로 남김: LTE 폰 경로 무회귀(사용자 폰 실기 때 확인), 직접 P2P 는 이번 범위 밖(relay 로 접속됨).
