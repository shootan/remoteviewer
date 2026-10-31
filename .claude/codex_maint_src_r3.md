# maint r3 SOURCE 재검토 — verifier 종료 코드 호환 포함

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 maint task.

**NEEDS_CHANGES — M2의 외부255/127/1 처리와 종료 코드 호환은 수용. 내부 검사 오류를 false로 접어 확정 missing/REFUSED로 만드는 두 경계가 남는다.** M1·시험 키/fixture·exit4·자동원복 없음은 유지한다. 새 별건이나 배포/서버 권한 변경을 요구하는 판정이 아니다.

## 고정 범위/호환

- `fix/maint-r1` **919dbfba5c8f1b59d0fb16a33c61d34270a98afa**, 비교 **b1fd9c7..919dbfb** 전체4파일. HEAD 일치·tracked clean·diff whitespace rc0. 미추적 __pycache__는 보존했다.
- deploy.sh의 phase/존재 조회, verify CLI와 shared module, 새13g~13k, CLAUDE 원복 구분을 읽었다.
- 저장소 내 verifier 호출을 검색하고 `gnlink_deploy.sh` 사전검사·`gnlink_release.sh` 서명 뒤 보조검사를 대조했다. 이들은 **0 수락 / 모든 nonzero 중단**이므로 기존1에서3/4로 나뉘어도 fail-closed 동작은 같다. release_test도 nonzero를 기대하는 구조다. 이번deploy의 실패진단만3을 명시적으로 사용한다.
- 따라서 새0/2/3/4 CLI 계약 자체는 GO다. 검증용이 변조 거부를nonzero로 판정해온 방식도 호환된다. 저장소 밖의개인exact1 호출자는 확인하지 않았으므로 “모든외부호출자까지 호환”이라고 확대하지 않는다.

## M2-a: crypto.verify 내부 예외가 여전히3으로 분류됨

보고가 명시한 shared `update_manifest.js`의 한계를 확인했다. `verifySignature`는 `crypto.verify`의 예외를 catch해false로 반환하고, loadManifest는 이를 SignatureInvalid로 만든다. 새CLI의 try/catch는 **이미false로접힌예외를 볼수없어** REFUSED3을 반환한다. usableKey의 선행 확인은 키구성 실패를 구분하지만 crypto provider 실행 실패까지 증명하지 않는다.

Codex 직접 제한 검증:

- 실제919dbfb CLI 원문을 Node VM에서 실행했다. 정상공개0.2.163 manifest/sig·정상usable key를 쓰고, **crypto.verify만 예외를 던지게** 주입했다. shared module/CLI의 분류 로직을 복제하지 않았다.
- 결과: `cliExit=3`, `manifest REFUSED: SignatureInvalid`.
- 같은원문과signed pair를 보통Node로 실행하면 `manifest ok ... 0.2.163`, exit0이다. 실제pair가불량인것이 아니라 검사를못한경우가3으로접힌것이다. 운영키/서버/파일을변경하지않았다.

**이 알려진 한계를 does-not-verify/“clients will refuse”의 확정 근거로 수용하지 않는다.** M2는 바로 수행 불가와 거부의 분리였으므로 동일 보수 범위다.

보수는 작게 할 수 있다. 진단용 경계에서 실제 verify 실행 결과/예외를 보존해 CLI에 전달하거나, shared boolean API를 유지하려면 **그 API로 확정할 수 없는 SignatureInvalid를 unknown으로 남긴다**. 서버의 기존 boolean fail-closed·wire/클라이언트 정책을 바꾸라는 요구는 아니다. 단순 try/catch를 한 겹 더 씌우는 것으로는 false에 접힌 정보를 되살릴 수 없다. 진짜 거부·도구 예외의 각 반례와0/3/4·배포 pair-state를 함께 확인한다.

## M2-b: 타입단어를출력하는조회 안에서 실제파일검사 오류가 missing으로접힘

현재존재조회는 다음형태다.

```
if test -f manifest && test -f sig;
then echo gnlink-pair-present;
else echo gnlink-pair-missing;
fi
```

외부 SSH255는 정확히 unknown이 됐다. 그러나 **내부 파일 검사가 수행 불가/오류여도 else가 missing을 출력하며 최종 exit0**이 된다. outer 조회가 돌았다는 것과 파일이 없음을 실제로 확인했다는 것은 다르다. 접근/metadata 검사 오류를 부재로 확정할 수 없다. phase 조회도 같은 test 기반의 경계다.

Codex는919dbfb에서 **조회 명령 원문을 추출**해, 실제 존재하는 공개 pair를 대상으로 내부 test가2를 반환하는 fault를 주었다. 결과는 **query exit0 / gnlink-pair-missing**, 실제 파일은 존재한다. 이는 조회 내부 primitive의 오류 주입이며 NAS의 실권한 장애를 새로 일으킨 시험은 아니다.

필요: 조회 계층에서 **실제 부재/형식 불충족과 metadata 조회 실패를 구분**한다. 오류 원인을 보존하는 stat/파일 API의 작은 read-only query를 쓰거나, 구분이 안 되면 unknown으로 남긴다. 타입 단어를 감싼 바깥 exit0만으로 성공을 만들지 않는다. 내부 probe 실패→unknown·진짜 missing→incomplete·정상 present를 검사하고, phase도 확인 못한 상태는 unknown으로 둔다. 새 상시 진단 서비스/설정/권한 완화는 필요 없다.

## 이미닫힌부분/시험평가

- 외부조회nonzero/예상밖출력→unknown, verifier127/1/4→unknown, 검증된same/different pair와단계분리, 상태별수동복구문구는방향에맞다. M1을재오픈하지않는다.
- 정상0.2.163 문서에대한이번CLI 직접실행은exit0이었다. testkey생성/DPAPI보호·child공개키override·payload gate동일이름fixture는기존수용을유지한다.
- `.claude/test-tmp/maint_r3_run1.log`의최종PASS/rc0 및13g~13k의명시결과를읽었다. **92/0/0·대조7 FAIL은작업용실행귀속**이며Codex가전체suite를재실행하지않았다.
- 대조13g/13h는옛조회문에shim표지가없어원래fault를주입하지못한실패라는정정을수용한다. 그두FAIL을옛오분류재현으로세지않는다. 앞선Codex의함수probe가그옛경계를재현했고, 이번직접probe는위의더안쪽오류전파를검사했다. fault가실제로도달했는지도확인하는구조를유지한다.

검증용은 두 잔여 경계를 **같은 M2·같은 maint task의 한 유효 지시서**로 취합해 기존 작업용2에게 전달한다. 신규 코드/서버 범위를 불필요하게 넓히지 않고, 진단이 확정할 수 없는 부분을 unknown으로 보수적으로 남기는 선택도 가능하다. 보수 후 해당 diff/대표 반례·호출부 호환을 확인하고 저장소 안 독립 local 검사로 잇는다. 운영 게시 승인은 별도다.

Codex 제품 수정·제품 빌드·전체 deploy_test·SSH/NAS·운영키 사용·삭제·commit/push 없음. 직접 실행은 **실제 CLI/실제 조회 명령의 비변경 fault 검사·정상 공개 pair 검증·Git/호출부 확인**이다. 미추적 판정서 외 history/계획·Git MCP commit은 같은 구현 기록에 취합한다. 사용자 현재 조치 없음.
