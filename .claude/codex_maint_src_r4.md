# maint r4 — SOURCE 재검토

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 maint task.

**SOURCE OK — e58bb29의 M2-a·M2-b 보수를 수용하고 이전 NEEDS_CHANGES를 닫는다.** M1, CLI 종료 코드 구분, 시험 fixture 정비도 기존 수용을 유지한다. 독립 전체 EVIDENCE·main 통합·배포 승인은 별도다.

## 고정 범위

- `fix/maint-r1` **e58bb2964b192ad6cf164b74887495c356f69277**, 비교 **919dbfb..e58bb29** 전체 4파일(+131/-45). 검사 당시 HEAD 일치, tracked clean, diff --check 정상.
- `automation/gnlink_deploy.sh` SHA-256 **ecae5f38fcf105a3d7499745c1300b13245c705136b9f8923d4d2522be592155**.
- `automation/gnlink_verify_manifest.js` SHA-256 **ca67846e3bcc87f0782d3e01f5d031b8d0006819c7a516bb293b77cff54c85aa**.
- 보고와 함께 실제 diff·공용 검증 모듈·호출부·시험 및 복구 문구를 대조했다.

## M2-a — 모호한 SignatureInvalid의 재확인

CLI가 공용 모듈의 SignatureInvalid를 그대로 거부로 확정하지 않고 **같은 문서 bytes·서명·P-256 공개키·SHA-256·P1363 방식**으로 직접 재검사한다. 실제 false만 3, 예외나 첫 검사와 모순되는 true는 4다. 서명 형식 자체가 잘못된 경우의 3도 공용 모듈과 일치한다.

재검사가 true여도 원래 실패를 성공 0으로 승격하지 않는다. 공용 서버의 boolean API·검증 정책·클라이언트 wire는 변경하지 않았다. 따라서 단순 catch 중첩과 달리, 거부를 관찰했는지 다시 확인하는 진단 경계가 생겼다. 선행 키 구성 실패 4, 정상 검증 0, 변조 수락 검증기의 fail-closed 검사도 유지된다.

## M2-b — 내부 조회 실패를 부재로 확정하지 않음

원격 조회를 manifest 디렉터리의 **단일 read-only find 나열**로 바꾼 것을 수용한다. 원격 명령 nonzero면 출력을 폐기하고, 끝 표지도 확인해야 listed가 된다. 따라서 SSH 255뿐 아니라 find가 일부/전부 출력한 뒤 실패한 경우도 swap/pair 모두 unknown이다. 성공적으로 나열된 일반 파일 이름을 확인한 경우에만 incomplete나 단계 추정을 한다. `.tmp` 상태와 실제 서명 pair 상태의 분리는 유지한다.

원본 M2-b의 `test -f` 오류→else→성공 exit/missing 경계는 제거됐다. 조회 실패 뒤 cat/검증을 근거 없이 성공으로 꾸미지 않는다. 나열 후 두 파일을 읽고 검증·바이트 비교하는 기존 절차도 유지한다. 이 진단은 읽은 시점의 상태이며 이후 다른 쓰기까지 영구 보증하는 것은 아니다.

GNU find `-printf` 전제는 **검증용의 직접 read-only SSH 확인**(NAS GNU findutils 4.9.0 및 해당 디렉터리 타입 출력)을 근거로 수용한다. Codex가 NAS에서 재실행한 것은 아니다. 해당 명령을 실행할 수 없는 환경에서는 nonzero→unknown이며 부재로 확정하지 않는다.

## Codex 직접 제한 실행 — 14/14

검토용 `.claude/codex_maint_review/r4_boundary_probe.py`를 실행했다. 후보의 실제 publish_pair 함수 원문을 추출했고, CLI·공용 모듈·version_compare·공개키 소스는 동일 bytes로 저장소 안 sandbox에 복사했다. 공개 0.2.163 signed pair를 사용하며 운영 개인키는 사용하지 않았다. 원격 전송은 자기 sandbox의 local 함수로 치환했다.

| 경계 | 직접 결과 |
|---|---|
| 실제 CLI 정상 pair | 0 |
| 실제 문서 변조 / 서명 형식 오류 | 각각 3 |
| manifest 파일 읽기 불가 | 4 |
| 실제 crypto.verify 예외: 정상 pair / 변조 pair | 각각 4, 두 호출 도달 확인 |
| 첫 verify=false 후 실제 정상 true / 첫 호출 예외 후 실제 정상 true | 각각 4, 모순을 성공·거부로 확정하지 않음 |
| 같은 pair 재게시 중 sig rename 실패 | publish exit4, manifest-only / verified-this-release |
| 게시 pair가 없는 상태에서 sig rename 실패 | publish exit4, manifest-only / incomplete |
| 조회 255 / 실제 find 실행 뒤 강제 exit1 | 각각 exit4, unknown / unknown |
| 끝 표지까지 모두 출력한 뒤 원격 exit1 / 정상 나열이지만 끝 표지 없음 | 각각 exit4, unknown / unknown |

기존 pair가 있던 publish 시험에서는 게시 pair bytes도 그대로였다. fault 도달을 각각 확인했다. 여기서 exit4는 의도한 게시 실패 진단이며 시험 실패가 아니다. 조회/검증 분류 로직을 검토용으로 다시 구현해 통과시킨 것은 아니다.

실행 산출물은 **`.claude/codex_maint_review/r4-47b395fc/`**에 보존했다. probe SHA-256 **192e6dd6a582013605592299e4b62141638cdbe589bc97eb5f52dae561e1552e**. 운영 서버·사용자 설치·공유기·서명 키에는 접근/변경하지 않았다. publish_pair 원문의 임시 사본 정리는 새 sandbox의 `.claude/deploy-tmp/` 안에서만 발생했다.

## 작업용 시험 평가·호환·다음 단계

- `.claude/test-tmp/maint_r4_run1.log`를 읽고 **97 PASS / 0 FAIL / 0 SKIP**을 확인했다. 이것은 작업용 전체 suite 실행이며 Codex 직접 14건과 구분한다.
- `.claude/test-tmp/maint_r4_neg.log`는 91 PASS / 6 FAIL이다. **13k의 crypto throw 2건만 옛 919dbfb의 실제 오분류 재현**으로 센다. 13g/13h의 4 FAIL은 옛 코드에 새 find fault가 도달하지 않은 결과라는 보고의 정정을 수용한다. 그 4건을 결함 재현 수로 더하지 않는다.
- 새 시험의 fault 도달 표지와 기존 same-pair·another-pair·진짜 missing·진짜 거부·read-back 실패·정상 게시 사례가 유지된 것을 확인했다. 새 fixture/키는 기존 승인된 일회용 시험 경계이며 운영 키를 사용하지 않는다.
- 저장소 내 배포 사전검사/릴리스 호출은 0만 수락하고 모든 nonzero에 중단한다. r3에서 수용한 **0/2/3/4** 계약은 유지된다. 새로 검증 성공을 넓히지 않으며, deploy의 실패 진단만 3을 명시적 거부로 사용한다. 저장소 밖 exact-1 호출자까지 검사했다는 주장은 하지 않는다.
- 실패 exit4·자동 원복 없음·상태별 수동 복구 문서 방향을 유지한다. GNU find 확인 때문에 새 권한/서버 설정 변경은 필요 없다.

검증용은 **같은 e58bb29를 고정해 저장소 안 독립 sandbox에서 전체 deploy_test와 대표 fault를 실행**하고 후보 파일 해시·시험 키 격리·pair 보존을 연결한다. 통과 후 이 maintenance의 정상 통합/후속 절차로 이어간다. 이 SOURCE OK를 운영 게시 완료나 NAS 실제 장애 시험 성공으로 확대하지 않는다.

Codex 제품 편집·제품 빌드·전체 suite 재실행·NAS/SSH·운영 키 사용·commit/push·배포 없음. 새 미추적 판정서/검토 probe 외 history·계획·Git MCP 기록은 같은 maint 작업에 취합한다. 사용자 현재 조치 없음.
