from pathlib import Path
p=Path('D:/remote/remote/docs/구현계획.md')
entry='''

## 2026-09-12 Host/PC 스레드·복구 전수 점검 — 상태

- [x] [직접 조사 원장](host_pc_thread_recovery_audit_2026-09-12.md): Host13/PC13/공유10, 근거·우선순위·재현 조건·수정 완료 기준
- [x] [364파일 전수 검색·66파일 분기 추적 목록](host_pc_thread_recovery_inventory_2026-09-12.md) 및 고정 소스 해시
- [x] NAS4스트림·DXGI wedge7회·Viewer 종료 사례 대조와 [소형 프로브 원문](host_pc_thread_recovery_probes_2026-09-12.md) 보존
- [ ] Host startup/output/shutdown/캡처 재시작 실패의 유계 복구 구현·검증
- [ ] 모니터/창 대상 보존·마우스 release·PC 표시/세션 복구 구현·검증
- [ ] 비동기 owner·네트워크/pipe 취소·서버 durable 저장 경계 구현·검증
- [ ] 실제 GPU/UI/UAC/게임·반복 장애 및 장시간 복구 행동 검증
- [ ] 본 전수 점검 문서 Git MCP 커밋 (현재 도구 부재; push 보류)
'''
assert entry.splitlines()[2] not in p.read_text(encoding='utf-8-sig')
with p.open('a',encoding='utf-8',newline='') as f:f.write(entry)
print('Plan checklist appended')
