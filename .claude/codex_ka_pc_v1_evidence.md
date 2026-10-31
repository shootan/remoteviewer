# KA PC V1 — EVIDENCE 판정

2026-10-29. **V1 EVIDENCE OK. 기존 shell 수용분과 managed 근거를 합쳐 작업용 KA PC 시험 관문을 닫는다.** 최종 검증용 독립 실행·R2 통합/출하 관문을 닫는 판정은 아니다.

## 고정과 직접 확인

- Source: `320bc3636532aa4bc4d67c88dacdbdbfedd975cf` (ka-pc worktree의 HEAD/clean 확인).
- 보고: `.claude/report_ka_pc_impl_r1.md` 「V1 한정 실행 결과」, SHA-256 `152b4683e41f08dfd32a508f7a51f2ce6f5099551020018af7e8ab7d144dfb78`.
- 원문: `.claude/np_impl_logs/ka_managed_v1-1.log`, SHA-256 `8ef3167fbaff9fdc2dc129c8262054e478bccb0b6347c4bc6f2166b142c66155`.
- Managed: `163ee8f6cf6e72740570d71427233fa9d22c02324d6b1c173cf550581619560b`.
- 제품 산출물 3개도 기존 고정값과 직접 대조했다: Client `f14c5942…`, ViewerKaTest `bdff9363…`, Stream `f35143b2…`. Client 산출물의 hash 대조를 실제 일반 Client exe 실행 증거로 바꾸지 않는다.
- Run: `.claude/test-tmp/ka-managed-v1-1/run-9872-616745562/`.
- Codex 대조 기록: `.claude/codex_ka_pc_review/v1_320/evidence_audit.json`.

로그에서 **PASS 60 / FAIL 0, ALL PASS**를 직접 집계했다. 두 op 모두 현재 activation의 card 좌표 `24,78,320,293`에 대한 PrintWindow 판정 뒤 선택하고, 각각 **968ms / 892ms**에 같은 op의 Presented를 확인한 후 WGC 영상 판정을 통과했다. 이전의 창 chrome 색 개수만으로 흰 client를 통과시킨 주장을 재사용하지 않는다.

## 이미지 근거

원본 BMP 4장을 직접 디코드·hash 확인했고 열람한 PNG와 픽셀이 같은 것도 대조했다.

| 원본 파일 | 크기 | SHA-256 |
|---|---|---|
| A-round1-picker-client.bmp | 1584×860 | `4f936b3bce6c2619b3f779cfbe9b551e637b47af0ff95d4e1fd20318720891b7` |
| A-round2-picker-client.bmp | 1584×860 | `4f936b3bce6c2619b3f779cfbe9b551e637b47af0ff95d4e1fd20318720891b7` |
| A-round1-presented.bmp | 1586×893 | `cd995248b46e04e8e04f5c7616b06e7649ad16d71b4924a0202d6dc0360f5c17` |
| A-round2-presented.bmp | 1586×893 | `becb04539634fdb939a426c9572a73ff9e01bd074a5bf1d4ab1f8d7a220b523d` |

picker는 실제 제목·전체 화면 카드의 선택 테두리/체크·창 카드와 문구가 식별되는 client 렌더다. 백색 client가 아니다. 두 picker는 바이트까지 같지만 동일한 목록/배치의 결과이며, 각 op는 자기 Activate 이후의 card 로그와 별도 캡처 호출로 연결된다. 파일 hash의 차이를 현재 세대 증명 조건으로 삼을 이유는 없다.

선택 뒤 두 이미지는 picker가 아닌 영상 내용이고 서로 다른 프레임이다. 같은 op의 Presented 및 선택 순서와 함께 실제 영상 진행의 근거로 수용한다. 관련 없는 desktop 내용은 여기에 옮기거나 외부에 게시하지 않는다.

**문구 정정 1건:** 보고 표 첫 picker 행의 “입력 꺼짐”은 이미지와 다르다. Codex가 직접 열람한 표시와 검증용 메시지는 **“제어 연결됨 · 입력 켜짐”**이다. 이는 viewer 쪽 상태 표시이고 Host의 실제 OS 주입 OFF 근거가 아니다. 이 보고 문구만 고치면 되며 재실행이나 별도 SOURCE 왕복은 필요 없다. 제목/목록에 보이는 “미리보기 없음”도 thumbnail 기능 성공으로 해석하지 않는다.

## 실행 경계

`ka_v1_job_run.ps1`은 native 하네스만 검토된 e2e_job_run에 넣고, 자기 stop-file을 실행 전에 생성해 600초 상한을 적용했다. 슬롯 소유 PS가 그 job 밖에 있으며 finally의 자기 슬롯 회수 구조도 확인했다. 로그 끝은 **`child_exit=0 residual=0 forced=0 ended_confirmed=1`**이다. 보고의 74초는 상한 안이며 시간 초과나 강제 종료 성공으로 처리한 경우가 아니다.

run 안 child trailer **10개 모두** producer end confirmed·EOF(error 0)·log kept를 직접 확인했다. viewer-C의 crash-close는 앞 r3에서 승인한 root + 시스템 conhost 구성원 예외와 root handle 종료 근거다. 이를 닫힌 뒤 conhost 각각의 종료를 직접 봤다고 확대하지 않는다. 이번 outer job의 residual 0은 별도 최종 확인이다.

Host 원문에서 시작 시 power off·input none 두 줄을 확인했고, managed의 완전한 Host 출력 **132082 bytes에 대한 end/EOF/OFF/무상충 최종 판정**도 통과했다. 화면의 viewer “입력 켜짐” 문구와 이 Host 실행 정책을 혼동하지 않는다. old-host는 NOT RUN이다. 슬롯 해제 시각·기존 사용자 PID 무접촉은 보존된 작업용 실행 보고에 귀속하며 Codex가 당시 프로세스를 관측했다고 하지 않는다.

## 닫힘과 남은 범위

닫는 것은 **화면 밖 실제 activation HWND의 picker client 렌더 → 그 카드의 PostMessage 선택 → 같은 op Presented/WGC 영상**의 근거 부족(V1)이다. 물리 모니터 표시·가림·자동 WM_PAINT 완료·실 사용자 클릭의 성공으로 표현하지 않는다.

이에 따라 앞서 수용한 shell 148(`aecbdfd` 실행 binary `9d0baa96…`) 및 managed protocol/lifecycle/crash 근거와 합쳐 **작업용 KA PC 시험 관문은 완료**다. 과거 managed FAIL·백색 BMP·잘못된 옛 visual assertion 기록은 그대로 보존한다. 이번 60을 shell과 더해 하나의 새 독립 전체 suite로 부르거나 shell을 새 binary로 재실행했다고 하지 않는다. V1 보완을 위해 추가 반복은 필요 없다.

검증용의 PC 최종 독립 확인·R2 통합, 실 NAT/비용·실 clipboard·old-host 및 기존 미검증은 별도 유지한다. APK decoder/control liveness와 예상 밖 peer_lost로 인한 **APK 0.2.33 출하 보류도 유지**한다. 이번 승인은 제품 게시·설치·push 승인이 아니다. 다음 담당은 검증용이며 기존 R2 통합 검증 흐름에 이 판정을 연결한다.

Codex는 제출된 source/hash/log/trailer/BMP·PNG를 읽고 대조했으며 Host/Viewer/AVD를 실행하지 않았다. 관련 없는 사용자 화면 내용을 재게시하지 않았고, 제품 수정·삭제·설치·운영·commit/push도 없다. 운영 검토 artifact만 작성했으므로 별도 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
