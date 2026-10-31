# relay-wire-ip 서버 게시 근거 확인

- 확인 시각: 2026-10-11 03:50:48 UTC
- source/build commit: `38af8ca09b8b0ad1fbf8574d1283e07bae42d8e0`.
- Codex 직접: main worktree HEAD와 `git ls-remote origin refs/heads/main` 모두 위 commit.
- Codex 직접: NAS `/opt/gnlink/remote60-directory`의 runtime 8파일 SHA256이 해당 commit의 git LF blob 8개와 전부 일치.
- 변경 파일: server.js `fdc9cbe8cdfc0015005a2074f90893099751bae45997429c4542c672dea4af7a`, relay_diag.js `0fe629172bf9caf8dfc7b66734b01159c6d9f5e98268b43384609322e9edb199`. 나머지는 accounts.js/device_credentials.js/update_manifest.js/version_compare.js/wake_target.js/package.json.
- Codex 직접: gnlink user service `remote60-directory` active/running, MainPID=2951580, ActiveEnterTimestamp=2026-10-11 12:43:18 KST.
- Codex 직접: 해당 PID의 journal에서 같은 시각 relay ENABLED 및 UDP29190 listening 확인.
- Codex 직접: 익명 HTTPS `https://gnlink.shotan.net/healthz` HTTP200, 응답 `ok:true`, observe port29181.
- Codex 직접: 이 PC의 별도 UDP socket으로 `gnlink.shotan.net:29181`에 일회 OBSERVE 전송, 목적 server IP/port에서 유효한 observation 응답 수신. 인증 connect나 relay session은 만들지 않았다.

**서버 파일 교체·기동·HTTPS/UDP OBSERVE 확인에 이어, Home→회사 relay 연결 성립과 사용자 화면 확인까지 추가됐다.**

전체 suite 실행·업로드·교체·재시작·백업은 검증용 수행으로 인용하며, Codex가 수행한 것으로 쓰지 않는다. 수정된 배포 스크립트의 모든 경로나 rollback 실행을 검증했다는 뜻도 아니다.

## 현장 결과 추가 확인 — 2026-10-11 15:01 KST

- 사용자 보고(검증용 인계): “회사로 접속 완료”, 집 PC에서 회사 PC 화면 확인.
- Codex가 NAS journal을 직접 대조한 connectId: `2ef608dd`, PID2951580.
- 15:01:00 `eligible client=175.207.45.151:40307 wire=192.168.0.1` 및 `punch from 192.168.0.1:40307`.
- 15:01:02 `bound client=192.168.0.1:40307 host=211.218.222.1:43000` → first c2h/h2c → `active; helloAck in 3ms`.
- 이후 집계에서 양방향 전달 c2h212 packets/22857B, h2c15107 packets/17429011B 확인. 이 session은15:02:15에 client-silent로 정리됨. 이 종료 원인을 사용자 종료 또는 새 결함이라고 추가 단정하지 않는다.
- 실제 relay ingress가 관측 wire 주소와 일치했으므로 이번 주소 결합 설계의 현장 전제가 충족됐다. 수정 전의 eligibility 뒤 무응답과 달리 연결·전달 사슬이 성립했고 사용자 화면 확인과 부합한다.

**남은 확인:** LTE 폰 경로 회귀, 별도로 명시된 실제 키보드/마우스 입력 성공 여부. 현재 “화면 확인” 보고를 입력 실기까지 확인한 것으로 확대하지 않는다. 직접 P2P 및 장시간 안정성은 이번 연결 복구 확인의 범위 밖이다. 추가 사용자 질문이나 A2A ACK는 보내지 않았다.

운영 파일 변경·재시작·rollback·사용자 PC 조작은 실행하지 않았다. A2A의 답장 불필요 요청에 따라 확인 ACK는 보내지 않았다.
