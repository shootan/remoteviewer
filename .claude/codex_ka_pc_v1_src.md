# KA PC V1 — 320bc36 SOURCE / 한정 실행 판정

- 검토일: 2026-10-29
- 후보: `C:/work/gnlink-wt/ka-pc` / `320bc3636532aa4bc4d67c88dacdbdbfedd975cf` (tracked clean)
- 보고: `.claude/report_ka_pc_impl_r1.md` V1 절, SHA-256 `ef0ef51e7edb3e37903a0e8fe76230127dec8d060d73deafaa234f3bb8d589ed`
- managed exe: `163ee8f6cf6e72740570d71427233fa9d22c02324d6b1c173cf550581619560b`
- 앞 판정: `.claude/codex_ka_pc_evidence_candidate_r2.md`

## 판정

**SOURCE OK — 아래 실행 경계 안에서 managed 1회 실행 가능. 아직 V1 EVIDENCE OK는 아니다.** PrintWindow를 현재 activation의 실제 HWND가 picker client 내용을 렌더하는 근거로 수용한다. 기존 합의인 화면 밖 창의 렌더·선택 검증 범위에 맞는다. 사용자 모니터에 창을 옮기거나 제품 render seam을 추가할 필요는 없다.

PrintWindow가 얻는 것은 대상 창이 지정 DC에 그린 결과다. 물리 모니터의 합성 결과, 가림 여부, 사용자 눈에 보인 상태, 자동 WM_PAINT가 이미 정상 완료됐다는 증거와 같지 않다. Microsoft 문서도 이 API를 대상 창의 DC 렌더 요청이며 동기식·블로킹 호출로 설명한다. [PrintWindow 공식 문서](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-printwindow)

따라서 로그의 “on the screen”, “visible”은 물리 화면 관측으로 인용하지 않는다. 최종 표현은 **“화면 밖 실제 activation HWND의 picker client를 PrintWindow로 렌더해 내용과 선택 대상을 확인했고, 선택 뒤 같은 op의 Presented와 WGC 영상 내용을 확인했다. 물리 모니터 표시·가림·실제 사용자 클릭은 미검증”**으로 한다. 입력은 하네스의 PostMessage 선택이며 물리 입력 시험으로 부르지 않는다.

## Source 확인

5f0e7a3..320bc36은 시험 5파일 +564/-22다. 제품·UI·render seam·창 위치·공용 window capture 변경은 없고 diff --check가 통과했다.

새 picker 판정은 창 chrome 전체 색 수 대신 현재 Activate 이후의 desktopCard 좌표, client 배경, card 테두리와 내부 내용을 본다. 실패하면 카드를 누르지 않는다. 새 activation마다 HWND와 op를 연결하고, 선택 뒤 같은 op의 Presented를 기다린 다음 WGC 내부 이미지가 picker/단색이 아닌지 본다. 이미지의 색 종류 자체는 새 프레임의 세대·시각을 증명하지 않으므로 op/Presented/실제 이미지와 함께 판단한다.

Codex는 고정 헤더로 순수 probe를 빌드·실행했다. 기존 흰 client BMP 2개 거부, 합성 picker 수용, picker의 영상 판정 거부, 기존 영상 이미지의 영상 판정 수용으로 **5/5 통과**했다. 기록은 `.claude/codex_ka_pc_review/v1_320/{judge_probe.cpp,build.log,run.log}`다. 이 probe는 Host·Viewer·창·GPU·PrintWindow를 실행하지 않았다. 합성 picker 통과를 실제 viewer 렌더 성공으로 대신하지 않는다.

작업용의 offscreen GDI/flip probe는 WGC가 GDI 내용을 읽지 못하는 캡처 한계를 뒷받침한다. 이것만으로 과거 실제 viewer가 정상 렌더했다고 소급 확정하지 않는다. 실제 두 activation의 새 BMP로 V1을 판정한다. offscreen probe 자체는 D3D/창을 사용하는 시험이며 향후 실행도 기존 GPU 슬롯 규칙을 따른다.

## 이번 1회 실행 경계

1. 기존 슬롯·전용 run/UDF·Host 입력 none + power off·launch/final guard·종료/EOF 계약을 그대로 유지한다. 제품 3개 hash도 다시 고정한다. shell 148과 기존 managed lifecycle/crash 근거는 반복 승인 대상이 아니다.
2. 새 `print_client`는 동기 PrintWindow이므로 응답 없는 viewer에서 무기한 멈출 수 있다. **기존 슬롯 스크립트에서 native exe를 제한 없이 직접 호출하지 않는다.** native 하네스와 자식을 검증된 kill-on-close job 실행 외피 안에 두고 총 wall deadline을 둔다. 기존 검토된 `e2e_job_run.exe`(SHA-256 `dc4c5049e8e9d32a8c9096005a2702c95ef30cce2d3f1998bd4005da8c89462f`)를 재사용할 수 있다. 자기 run 안의 stop-file을 실행 전에 새로 만들고 `--grace-sec 600`으로 native 하네스만 실행하면 600초 상한을 둘 수 있다. 슬롯 소유 PowerShell은 이 종료 대상 job 밖에서 finally를 실행해야 한다. 시간 초과/강제 종료/잔여/종료 미확인은 실패 또는 INVALID이고 PASS로 세지 않는다. DIB/DC를 살아 있는 detached PrintWindow thread 아래에서 해제하는 우회는 하지 않는다.
3. `save_image_bmp` 반환값을 caller가 무시한다. 그러므로 assertion PASS만으로 이미지 보존을 주장할 수 없다. 결과 제출 때 두 picker-client BMP와 두 presented BMP의 실제 존재·디코드 가능·hash를 확인한다. 저장 실패/없음/깨짐이면 V1 근거 부족으로 남긴다. 이 확인은 후처리로 가능하며 이를 위해 제품을 변경할 필요는 없다.
4. 두 activation의 현재 HWND/op·좌표·PrintWindow 성공·배경/card 판정·BMP·선택·같은 op Presented·후속 WGC를 한 흐름으로 남긴다. 실제 BMP를 직접 열어 card/문구가 식별되는지도 확인한다. 색 표본 통과만으로 잘못된 모양/문구까지 정상이라고 확정하지 않는다. 사용자 desktop의 관련 없는 내용은 보고에 옮기지 않는다.

실행 외피와 보존 조건을 검증용이 적용하면 같은 승인 범위에서 1회 진행한다. 외피를 적용하지 못하면 실행을 보류하고 그 구체 blocker만 연결한다. 새로운 제품/시험 commit을 요구하는 판정은 아니다.

## 종료 조건과 남는 범위

새 두 activation의 실제 캡처가 위 조건을 충족하면 **V1의 화면 밖 picker 렌더·선택 근거 부족을 닫을 수 있다.** 그때 기존 shell/managed 근거와 합쳐 작업용 KA PC 시험 관문의 제한된 완료를 판정한다. 현재는 새 실행 전이므로 닫지 않는다.

PC 최종 독립 검증·R2 통합/출하, 실 NAT·실 clipboard·old-host NOT RUN, 물리 모니터 표시 등 기존 미검증은 그대로 남는다. APK decoder/control liveness로 인한 R2 APK 0.2.33 출하 보류도 별개로 유지한다.

이번 Codex 작업은 source/hash·기존 자료 읽기와 순수 판정 probe뿐이다. Host/Viewer/AVD 실행·사용자 입력·설치·운영·삭제·제품 편집·commit/push는 없다. 운영 검토 artifact이므로 별도 제품 commit을 만들지 않았다. 사용자 현재 조치 없음.
