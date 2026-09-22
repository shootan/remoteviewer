# 안정화 후보 최종 검토: MERGE_OK / 배포 보류

**검토 task `t-nbc17io0` 완료. 제품 전체 장애 해결 또는 배포 완료와는 다르다.**

- 코드·문서 머지 판정: **MERGE_OK**. 기존 사용자 작업을 덮지 않는 clean main 작업 트리에서 통합 가능.
- 배포 판정: **보류**. 게임 조건에서의 GPU 작업량(C) 미측정, 실제 GPU/게임·WAN·다중 실세션·장시간 검증 잔여.
- 실제 실행: commit/main merge/설치/배포/push **미수행**. 검토 요청을 병합 승인으로 해석하지 않았다.

## 판정 근거

검증용 Claude `remote#wl89o85z`가 revision 2를 별도 `build-review-stability`에서 직접 빌드·실행했다. **11개 타깃 전부 exit 0**: ingress, 환경 snapshot, WndProc(42/0), UDP 복구(24 시나리오), FrameGate, 최종 캡처 픽셀, readback, liveness, shared core, kick, ABR. gate의 missing=2 / zero-present·freeze·stutter=1 / healthy=0도 검증용이 별도로 실행해 확인했다. 이는 revision 1의 제출 로그 인용과 구분된다.

검증용의 남은 조건은 **history의 UTF-8 BOM 복원 3바이트뿐**이었고, 그 외 재검토는 불필요하다고 명시했다. 뒤늦게 도착한 해당 판정은 문서 보수 전 manifest `c08e7957...`를 기준으로 했다.

Codex는 이미 보완된 `25a48831ba4e41544ea672dc939de6446ece186e73c64718b595d285067c35f0` 후보에서 다음을 직접 재확인했다.

1. history 선두 **EF BB BF**, 마감 기록 추가 전 raw SHA-256 `71c6aeb8effe280239e089718afa118189df5a00eaae486b67e098c629f941aa`.
2. 선두 3바이트를 제외한 history 본문은 보수 전 revision 2 입력과 **바이트 동일**.
3. 검증용이 검사한 revision 2와 **소스 45개·제품 실행 파일 4개 해시 변화 0**, 실제 디스크와도 일치.
4. 주 트리 history/계획은 각각 `b4d195f9...`, `bd451a5d...`로 그대로이며 내용 손실 없음.
5. 별도 검증 트리의 11개 테스트 실행 파일 존재와 kick/ABR 기준선 해시 일치 확인. 11개 실행 종료 결과 자체는 검증용의 독립 실행 보고이며 Codex가 다시 실행한 수치로 바꾸지 않는다.

따라서 검증용의 **“BOM 3바이트 복원 → MERGE_OK” 조건 충족**으로 최종 머지 가능 판정을 확정했다. 검증용에게 조건 충족 근거를 전달했고 불필요한 전량 재검토는 요청하지 않았다.

## 보존된 승인 대상

검토한 manifest와 소스·문서·제품 파일을 `build-incident/approved-review-r2/`에 불변 사본으로 보존했다. 원본 파일은 바꾸지 않았다. 이 폴더의 manifest SHA-256은 위 `25a48831...`이다. 판정 이후 이력/계획/본 문서에 추가하는 마감 기록은 제품 변경이 아니다.

후보 branch는 `fix/remote-game-stability`, 기준은 main과 동일한 `1261ef580d5a1dbeec1a14723f2d1564834c0a95`다. **새 수정 커밋은 아직 없다.** Git MCP 도구 미제공 및 현재 검토 한정 범위로 commit/merge는 수행하지 않았다. 이후 병합 시 현재 main 이동 여부, 승인 대상 소스/실행 파일 해시, 문서 소유 작업의 통합 범위를 다시 확인한다.

## 배포 전 남은 C

최신 픽셀을 버리지 않도록 content를 받아들이면서 입력 변경률이 목표 소비율보다 높을 때 GPU 복사/전처리 호출이 늘어난다. **144Hz 입력·30fps 소비 조건의 약 4.8배는 호출량 추정이지 실측 GPU 사용률이 아니다.** 버려질 프레임의 색공간 변환/전처리 이연과 실제 GPU 부하 측정이 필요하다. 이 검토의 MERGE_OK를 배포 OK로 전용하지 않는다.
