# Remote60 사용자 테스트 체크리스트 (M8)

## 0) 사전 준비
- 작업 경로: `C:\work\remote\remote60`
- 빌드 완료 확인:
  - `cmake --build --preset debug-vcpkg`
- 실행 파일:
  - `build-vcpkg-local\apps\host\Debug\remote60_host.exe`
  - `build-vcpkg-local\apps\client\Debug\remote60_client.exe`
- signaling:
  - `node apps/signaling/server.js`

## 1) 실사용(브라우저 클라이언트) 빠른 실행
1. one-shot 실행(권장)
   - `powershell -ExecutionPolicy Bypass -File automation\run_web_runtime.ps1 -OpenBrowser`
2. 수동 실행
   - 터미널 A: `node apps/signaling/server.js`
   - 터미널 B: `build-vcpkg-local\apps\host\Debug\remote60_host.exe --runtime-server`
   - 브라우저: `http://127.0.0.1:3000`
3. 브라우저에서 `Connect` 클릭 후:
   - 화면 표시 확인
   - 영상 클릭 후 마우스/키 입력 동작 확인
4. 종료
   - one-shot 실행 시 `Ctrl+C`
   - 수동 실행 시 signaling/host 프로세스 각각 종료

## 2) 기본 기능 스모크
1. video e2e
   - host 실행
   - client: `--video-e2e-probe`
   - 기대: client exit=0, host 로그에 `sent=answer`
2. audio e2e
   - host 실행
   - client: `--audio-e2e-probe`
   - 기대: client exit=0
3. input e2e
   - host 실행
   - client: `--input-e2e-probe`
   - 기대: client exit=0

## 3) 오디오 실경로 확인 (M5)
- `remote60_host.exe --audio-loop-probe 10`
- 기대:
  - `detail=ok`
  - `opusFrames20ms > 0`
  - `rtpPackets > 0`
  - `opusBytes > 0`

## 4) 실시간 미디어 확인 (M7)
- `remote60_host.exe --realtime-media-probe`
- 기대:
  - `detail=ok`
  - `videoRtpSent > 0`
  - `audioRtpSent > 0`

## 5) 즉시 핫픽스 절차
1. 수정 후 즉시 빌드
   - `cmake --build --preset debug-vcpkg`
2. 최소 회귀
   - `--audio-loop-probe 3`
   - `--realtime-media-probe`
   - video/audio/input e2e 각 1회
3. 로그 저장
   - `logs\` 하위에 날짜 접두어로 저장
4. 롤백
   - Git 저장소인 경우: `git -C C:\work\remote\remote60 checkout -- <file>`

## 6) 장애 대응 빠른 판단
- signaling 포트 충돌: `E_PORT_IN_USE`
- 등록 누락: `E_REGISTER_FIRST`
- peer 미연결: `E_PEER_NOT_CONNECTED`
- 클라이언트 not_ready:
  - `E_NOT_REGISTERED`
  - `E_PEER_OFFLINE`
