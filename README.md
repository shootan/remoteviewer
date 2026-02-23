# remote60

개인용 원격 PC(호스트/클라이언트) 프로젝트.

## 현재 단계
- WebRTC runtime 경로(영상/오디오/입력) 동작
- 시그널링 서버(Node.js) + 브라우저 웹 클라이언트 제공
- 회귀 스크립트(`verify_runtime_suite.ps1`, `sig_restart_verify.ps1`) 통과

## 의존성 부트스트랩 (vcpkg manifest)
프로젝트 루트에 `vcpkg.json` 추가됨:
- libdatachannel
- opus
- nlohmann-json

## 빌드 (Windows)
```powershell
cmake --preset windows-vs2022-vcpkg
cmake --build --preset debug-vcpkg --target remote60_host remote60_client
```

## 빠른 실행(권장)
```powershell
powershell -ExecutionPolicy Bypass -File automation\run_web_runtime.ps1 -OpenBrowser
```

즉시 반환(백그라운드 기동) 방식:
```powershell
powershell -ExecutionPolicy Bypass -File automation\start_web_runtime.ps1 -Port 3014
powershell -ExecutionPolicy Bypass -File automation\status_web_runtime.ps1
powershell -ExecutionPolicy Bypass -File automation\stop_web_runtime.ps1
```

기본 실행 결과:
- signaling: `node apps/signaling/server.js` (기본 포트 `3000`)
- host: `build-vcpkg-local\apps\host\Debug\remote60_host.exe --runtime-server`
- web client: `http://127.0.0.1:3000`

## 수동 실행
```powershell
# 터미널 A
node apps/signaling/server.js

# 터미널 B
build-vcpkg-local\apps\host\Debug\remote60_host.exe --runtime-server
```

브라우저에서 `http://127.0.0.1:3000` 접속 후 `Connect`.

## 자동 검증
```powershell
powershell -ExecutionPolicy Bypass -File automation\verify_runtime_suite.ps1
powershell -ExecutionPolicy Bypass -File automation\sig_restart_verify.ps1 -ReconnectTimeoutSec 70 -InitialConnectTimeoutSec 20 -ClientSeconds 8
```

## 창 기반 모드(Experimental)
- 창 단위 원격 제어 설계/토글/롤백 절차는 `구현.md`를 참고하세요.
- 런타임 롤백: `REMOTE60_WINDOW_MODE=0`
- 빌드 롤백: `REMOTE60_ENABLE_WINDOW_MODE=0`
