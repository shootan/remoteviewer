# KA PC managed/shell 시험 SOURCE — ce0a672

**NEEDS_CHANGES — Host guard 연결은 수용한다. shell WebView 프로필의 실제 격리와 모든 child의 실패/완료 판정을 보수한 뒤 두 하네스를 실행한다.** PC 제품 cd9937a의 SOURCE OK와 공용 b8 guard의 지정 8개+fatal 수용은 유지한다. 이번 두 PC 시험을 실행하지 않았다.

## 고정·읽기 범위

- 후보 **ce0a672**, 비교 **c0271a7..ce0a672**의 ka_pc_managed_e2e_test.cpp·ka_pc_shell_e2e_test.cpp 두 파일(+153/-80). tracked clean·diff --check 정상.
- c0271a7 parents는 **cd9937a8712b305498e32acaefda0dc2289c955f**와 **b8dcc930ea1a2e99951ec7fbcd5fa339d5a04c3b**다. 해당 merge에서 client_shell_main/viewer_*·ui 변경이 없음을 읽기 확인했다. 이 검토는 별도 merge 제품 회귀/최종 통합 EVIDENCE까지 대신하지 않는다.
- Child.Start/Guard/End·root/env 설정·실제 product create_shell_webview 및 SDK 계약을 읽었다. 제품/시험 빌드·WebView/Host/Viewer/UI 실행·사용자 profile 열람/삭제는 하지 않았다.

## 수용한 연결

- 성공한 child는 suspended 상태에서 **공용 e2e_adopt_or_invalid**로 job/kill-on-close/편입을 확인한 뒤 Resume한다. 수집 thread가 공용 e2e_read_to_end의 EOF/Error/Cut과 오류 코드를 기록한다.
- Host의 두 OFF gate 뒤 monitor 생성, producer 종료 확인→reader join→동일 e2e_final_pipe→Finalize→job close 순서는 검수된 b8 helper를 변형하지 않고 연결했다. 결과 앞 final_check_all도 추가했다.
- 신규 run 폴더를 CreateDirectory의 신규 성공으로 확보하고 old 0.2.163 case를 NOT RUN으로 남긴 방향을 수용한다. approved Host/MigrationTest 지원 여부·artifact hash·슬롯/창 격리는 실행 시 그대로 확인한다.
- 위 수용은 **N1/N2의 실제 미확인 오류를 성공으로 처리해도 된다는 뜻은 아니다.**

## N1 — TEMP/TMP만으로 WebView profile 격리를 완료했다고 볼 수 없음

제품 create_shell_webview(client_shell_main.cpp :2645–)는 **GetTempPathW + GNLinkClient**를 CreateCoreWebView2EnvironmentWithOptions의 userDataFolder로 전달한다. ce는 부모 test process의 TEMP/TMP를 run/temp로 바꾸므로 **그 API가 그 환경을 정상 사용하면 요청 경로를 바꾸는 효과**는 있다.

그러나 Microsoft의 계약상 **WEBVIEW2_USER_DATA_FOLDER와 registry override가 API 인자 경로를 대체할 수 있다.** 추가 browser argument/런타임 선택도 별도 override 대상이다. 따라서 ambient WebView 설정을 그대로 상속한 채 TEMP만 바꾸면 유효 UDF가 run 안이라는 보장이 없다. [Microsoft Win32 environment 생성 계약](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/webview2-idl?view=webview2-1.0.4129.50).

또한 현재 CreateDirectoryW(TEMP 폴더)·SetEnvironmentVariableW(TEMP/TMP/LOCALAPPDATA)의 결과를 확인하지 않고, **실제 GetTempPathW 반환 경로나 생성된 environment의 UserDataFolder를 판정하지 않는다.** 제품 함수도 MAX_PATH 반환/연결 실패를 확인하지 않으므로 시험은 실제 인자를 사전에 검증해야 한다. Source만으로 “사용자 %TEMP%를 사용하지 않았다”는 완료 주장을 하지 않는다.

### 같은 시험 task의 보수 범위

1. **기동 전 preflight:** 정확한 repository 내부 root·신규/non-reparse run과 temp/profile 경계를 확인한다. directory/env setter 성공·길이·Win32 GetTempPathW의 실제 resolve가 예상 private root와 일치하는지 검증한다. 알 수 없음/다른 경로면 WebView 생성 전에 INVALID다. 단순 env 문자열만 증거로 쓰지 않는다.
2. **WebView 설정을 test process에서 고정:** 별도 신규 UDF를 run 안에 정하고 **WEBVIEW2_USER_DATA_FOLDER도 그 경로로 명시**한다. 적용 가능한 environment/registry override와 추가 arguments/런타임 경로가 profile/cache/log 등을 사용자 경로로 돌리지 않는지 확인한다. 알 수 없는 override는 생성 전에 거부한다. 사용자/machine registry를 지우거나 광역 변경하지 않는다. 필요한 경우 기존 test seam에서 생성 인자/관측을 연결하고 normal build의 동작은 보존한다.
3. **유효 경로 확인:** 환경 생성 callback의 **ICoreWebView2Environment7::get_UserDataFolder**는 실제 절대 UDF 경로를 돌려준다. 이를 예상 신규 UDF 및 repository 경계와 대조하고 근거로 남긴다. 불일치/조회 실패면 controller의 페이지 navigation/sign-in/후속 client를 진행하지 않는다. [Microsoft UserDataFolder getter](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2environment7?view=webview2-1.0.4129.50).
4. **사전·사후 역할 구분:** environment 생성은 UDF를 이미 만들 수 있으므로 **사후 getter만으로 “다른 profile을 전혀 건드리지 않음”을 증명할 수 없다.** preflight로 사용자 경로 선택을 막고, getter는 그 실제 적용 확인이다. 사후 불일치가 나오면 이미 닿았을 가능성을 기록하며 자동 cleanup하지 않는다.

환경 override 불일치·setter/GetTemp 실패의 대조는 resolver/생성 API 대역으로 **CreateEnvironment 호출 0**을 확인한다. 실제 양성 WebView는 검증된 private UDF 하나로만 만들고 effective path를 확인한다. 실제 사용자 profile을 일부러 지정해 거부를 관찰하지 않는다. 필요하면 서로 다른 **소유 test root 두 개**/가짜 environment getter로 mismatch 경로를 검증한다.

### “TEMP 외 경로”의 정확한 범위

UDF는 cookies·permissions·cache/DOM storage 같은 browser 데이터를 담고, 같은 UDF를 쓰는 WebView들은 **서로 다른 host app이어도 같은 WebView session을 공유할 수 있다.** [Microsoft UDF 설명](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/user-data-folder). 이 시험에서는 사용자 GNLinkClient의 UDF를 쓰지 않는 것이 핵심이다. 다른 Microsoft Edge profile까지 공유됐다고 추정하지 않는다.

**UDF 격리는 기계 전체 filesystem sandbox는 아니다.** 예를 들어 download의 기본 경로는 별도 profile 설정이고 기본값은 사용자의 download 위치다. [Microsoft download 경로 계약](https://learn.microsoft.com/en-us/dotnet/api/microsoft.web.webview2.core.corewebview2profile.defaultdownloadfolderpath?view=webview2-dotnet-1.0.3856.49). 이 sign-in/card 시험에서 예기치 않은 download/file dialog·추가 외부 목적지를 정상 성공으로 진행하지 않고 취소/실패로 남기며, 허용된 산출물은 자기 root로만 연결한다. 설치된 runtime/OS의 공용 동작까지 전부 repository 파일이라고 주장하지 않는다. 같은 runtime binary의 사용과 같은 UDF의 사용도 구분한다.

이번에는 profile을 evidence로 **보존**하면 된다. 이후 정리가 필요해도 자기 UDF 소유/경계를 검증하고 모든 controller/browser process가 끝난 뒤에만 한다. 기존 사용자 UDF의 삭제/초기화는 하지 않는다. Microsoft도 UDF 정리 전에 WebView session과 browser process의 종료를 요구한다. [UDF 수명과 정리](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/user-data-folder#deleting-user-data-folders).

## N2 — 비Host child의 종료/출력 실패와 launch 설정 실패도 실패로 연결

공통 Child.End는 **monitor가 있는 Host만** Finalize로 producerEnded/EOF를 거부한다. Node·Viewer 등 monitor 없는 실제 child는 producerEnded=false/readResult=Error/Cut이어도 handle을 닫고 log를 쓴 뒤 void로 끝난다. 종료/완료 실패가 gFailures/INVALID에 전달되지 않아, 앞 기능 assertion들이 통과했으면 마지막에 ALL PASS가 가능하다.

**실제로 시작한 모든 child의 종료·출력 완료를 role에 맞게 판정**한다. 비Host에 Host의 input/power OFF 문구를 요구하지 말고 generic producer/EOF 상태를 검증한다. 종료/수집 미확인은 실패/INVALID·부분 자료 보존으로 연결한다. NOT RUN/default-constructed child·이미 종료 정리한 child와 구분해 End를 idempotent하게 한다.

Start의 **NUL/pipe handle 설정·attribute list 초기화·HANDLE_LIST 설치 결과**도 확인한다. 현재 Init/UpdateProcThreadAttribute 실패를 버린 채 inheritHandles=TRUE로 CreateProcess를 진행한다. whitelist가 확인되지 않으면 child 생성/Resume으로 진행하지 않는다. 실패 시 자기 handle만 정리하며 PID/이름 kill이나 다른 Viewer를 건드리지 않는다.

읽은 부분 자료와 read 오류/종료 상태는 **가능한 INVALID 경로가 process를 끝내기 전에** 자기 run의 evidence로 남긴다. 현재 logPath에 stdout을 쓰는 것은 monitor Finalize 뒤라서, 그 자리의 invalid가 발생하면 메모리 text만 잃을 수 있다. owned log sink로 수집하거나 final snapshot을 판정 전에 보존하되 로그 쓰기 실패도 명시한다. 이 작업에 사용자 profile 내용은 수집하지 않는다.

대조는 실제 Host/WebView 없이 **self-sleeper/pipe/job·launch API 대역**으로 먼저 할 수 있다. nonHost end 미확인/read 오류→전체 성공 불가, whitelist 설정 실패→child 0, Host finalize tail/오류→partial log 보존을 확인한다. 검수된 공용 guard를 다시 변형할 필요는 없다.

## 과거 영향 기록 방식

**새 profile-isolation finding으로 사실·조건·미확인을 분리**한다.

| 분류 | 기록할 내용 |
|---|---|
| source 사실 | 이전 shell e2e도 product create_shell_webview의 GetTempPath+GNLinkClient 요청 경로를 사용했고, 이 ce 변경 전 test 자체에는 TEMP/TMP private 지정이 없었음 |
| 조건부 가능성 | 실제 당시 TEMP/override/desktop이 사용자 GNLinkClient와 같았다면 UDF/세션 공유 및 profile 데이터 사용·변경 가능성 |
| 실행 범위 | 기존 보존 로그의 run/commit/시간·호출된 shell e2e를 식별. 추정 run 수·파일 변경 수를 확정값으로 만들지 않음 |
| 미확인 | 실제 선택된 UDF, 동시 browser process 공유, 읽기/쓰기 파일·cookie/storage/계정 변화·손상 여부. 현재 파일의 존재/mtime만으로 과거 영향 0이나 손상을 증명하지 않음 |
| 보수·새 근거 | 새 private UDF preflight/effective getter·artifact/UI hash·새 독립 run과 scope. 이전 run을 소급해 isolated 성공이라고 바꾸지 않음 |

검증용이 기존 **docs/history**와 해당 PC e2e/상태 정본에 이 사실과 미확인을 취합한다. 기존 PC 실제 Host e2e는 이미 입력 주입 finding으로 완료 관문 사용 보류였으므로 **profile 격리 미확인 사유를 추가**한다. 제품 SOURCE·API/crypto·별도 gate/queue 순수 probe·게시 사실까지 일괄 무효로 만들지 않는다. 사용자 cookie/profile 열람·삭제·강제 종료·복구를 영향 확인 수단으로 사용하지 않는다. 확인 가능한 기존 test 로그·source/env 기록만 이용한다.

## 판정과 다음 단계

**ce0a672의 두 PC 하네스 실행은 N1/N2 보수 고정본까지 보류**다. managed 시험에는 WebView 생성이 없지만 공통 child 완료/설정 결과 판정은 양쪽에 적용한다. profile만을 이유로 제품 default UDF를 바꾸거나 PC 기능을 다시 설계할 필요는 없다. test process/기존 test seam의 격리로 해결한다.

검증용은 위 사항을 기존 작업용2의 **PC 시험 task 한 정본**으로 연결한다. 제품 cd9937a Source 수용·b8의 지정 8개+fatal·ordinary record lane은 유지한다. 새로운 WebView/Host 실행이나 사용자 profile/registry 조사·변경은 이번 Codex가 수행하지 않았다. 이번 작업은 source/공식 문서 읽기와 이 판정 기록이며 commit/push도 없다. 사용자 현재 조치 없음.
