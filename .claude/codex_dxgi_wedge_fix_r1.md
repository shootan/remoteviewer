# dxgi-wedge-fix r1 — Codex SOURCE/EVIDENCE 검토

2026-10-22 · task=t-dreqrszk / rev=r1 / kind=review-result
reply_to 검증용 `remote#wwml7h91`

**NEEDS_CHANGES.** 격리 우선과 prelogin 제외, 보호 저장소의 기본 경계, 시험/출하 MT 분리 및 minidump 보류는 대체로 계약대로다. 다만 B6의 **대상 동일성·실제 표시 후 확정**을 어기는 경로가 있어 후보158aff3의 구현/통합/출하 OK를 주지 않는다. 아래 D1~D4를 한 묶음으로 보수하고 E1의 영향 검증을 연결한다. 미검증NVIDIA/UAC를 핑계로 자동 보수·독립 UI 검증을 생략하지 않는다.

## 고정 후보와 직접 확인

- 작업용 `C:/work/gnlink-wt/fix-dxgi-quarantine`, HEAD158aff3(제품 마지막e711f08, base5063cd9), tracked clean/미추적.claude만. 7commit의51파일 변경목록과 관련 제품diff/소스·시험 일부를 읽었다. 검증용 worktree `verify-eop`의 기록/원본 로그와 별도 build-vs 산출물 hash를 대조했다.
- 직접 hash 확인: Stream=`9d85384ef348514dd330bb9773948a00dcf142b38ec0fda7b952620c653b34df`, Host=`3826b09361a0d7cb33b61cc6e6e0501bbac914c31ead0bb77d83bbb653815c9d`, Viewer=`3f661e79a6ba9043724481020f033f26ddbca8737f9e3c938ac667c425d353d8`, Client=`201ce84e59df2f258da9477e87b05e0ede4765c9d8db67890995590d256866b2`가 보고와 일치한다.
- 검증용 원본 끝줄을 직접 읽어 store53/0·supervisor23/0·restore26/0·snapshot7/0·recovery_process PASS, quarantine16/0·control15/0, gate9PASS, MtAb/MigrationTest로 대체한 부정 gate의FAIL을 확인했다. 실행rc0/부정rc1은 검증용 기록의 귀속이며 **내가 제품 테스트를 재실행한 것은 아니다**.
- 작업용 사슬 스크린샷 `.../dxgi-wedge-9KcLQn/shots/3-reconnecting-0.png`와 `4-restored.png`를 직접 열어 재연결 안내와 재귀 desktop 화면을 확인했다. **작업용이 만든 과거 실행 산출물의 독립 열람**이다. 내가 사슬을 독립 실행한 증거가 아니며 특정 monitor동일성/최초present시각·20.1초를 이미지 두 장만으로 재실증하지 않는다. 실제field release-wedge 재현과도 구분한다.

## D1 — B6: 대상 확인 실패를 현재 primary 선택으로 바꾸면 안 된다 (필수)

`viewer_restore.cpp:108~126`은 기록에 monitor identity가 있어도 새 host가 목록 미지원이면 `primary`를 근거로0을 선택하고, 목록이4초 안에 안 와도0을 선택한다. **이전primary가 제거/교체되거나 primary설정이 달라진 상태에서 목록이 누락되면 다른 화면을 자동 공개**한다. 계약은 gone/changed/ambiguous/identity미확인→picker다. e711f08의4초 우회는15초 지연을 줄였지만 그 계약을 완화한 변경이다.

또 `MatchRestoreMonitor`가 쓰는 name은 `host_window_enum.cpp:158~160`의 `MONITORINFOEX.szDevice`(`\\.\DISPLAYn`)와 rect뿐이다. 현재test는 목록index만 바뀌는 경우를 다루며 **같은GDI이름·같은rect 뒤에 다른 output이 오는 경우**의 동일성은 증명하지 않는다. 이것을 안정outputidentity라고 단정할 수 없다. Windows는 별도로 monitor target device path 등 정보를 제공한다([Microsoft target device name API](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-displayconfig_target_device_name)). 어떤 identity를 택하든 조회실패/모호함을 성공으로 바꾸지 않는다.

**보수 범위:** 실제 사용자가 선택한 **논리 desktop(현재primary를 뜻함)**과 **명시 monitor**를 구분한다. 전자는 그 의미로 복원 가능하지만 후자는 새 목록의 검증된 outputidentity와 대응해야 한다. 원래 논리desktop을 monitor목록 수신만으로 물리monitor로 바꾸거나 반대로 잃지 않는다. 명시monitor정보가 없으면 picker+사유로 끝낸다. 현재protocol에서 안정identity를 보장할 수 없으면 명시monitor는 자동복원을 보류하는 안전한 최소안도 허용한다(그 한계를 acceptance/문서에 표시). 안정identity를 새wire로 추가해야 한다면 그부분을 검증용 경유 설계delta로 묶고, 나머지 보수는 계속한다. **목록 전달을 고치거나 논리desktop 의미를 보존해서 사슬이 통과하게 해야지, 확인 실패를0선택으로 우회하지 않는다.**

필수 대조: monitor A가primary였던 복원descriptor→새host primaryB/목록누락·미지원, 같은DISPLAYname/rect이지만 다른output, 목록재순서·명시monitor제거·동일identity성공, 원래논리desktop성공. 원래 안전 규칙을 제거한 mutant가 반례에서 실제로 실패해야 한다.

## D2 — B6: decoded/reveal 이전에 ‘렌더됨·복원됨’을 보고한다 (필수)

`viewer_window_proc.cpp:1305`는 `InvalidateRect` 뒤 곧바로 `restore_on_reveal`을 호출하고, 그 함수(`viewer_restore.cpp:150~163`)가 **Done/result=restored와shell대상보고**를 확정한다. WM_PAINT/present성공은 아직 아니다. toolbar 변경도 `restore_frame_decoded`→kMsgRestoreReport로 **decode**만 보고 현재target을 내보낸다. 화면목록 응답 때 `restore_step`의report_current도 최신 decoded/present된 대상과 대조하지 않는다.

반례: host선택확인·새frame decode→reveal message처리→실제present실패/아직이전frame만표시→프로세스exit43. shell은 사용자가 한 번도 보지 못한 target을 ‘마지막렌더된대상’으로 복원한다. **이전화면을 본 뒤 다음선택진행중 실패**라는 계약반례를 현재26개 순수parse/match시험은 다루지 않는다.

**보수:** 기존선택게이트/화면공개절차를 필요없이 바꾸지 말고, restore descriptor의commit과Done보고를 **실제 성공한 현재frame present**에 묶는다. `viewer_present.cpp`는이미 `presented`와실제 `frameStreamGeneration/frameVersion`을 알고 있으므로 그metadata로 pendingtarget의generation/operation을 검증할 수 있다. 목록응답/선택ACK/decode단계에서 마지막성공descriptor를 덮지 않는다. metadata보강도 실제마지막present대상만 보강한다. 초기picker/reveal을실제로표시하기 위한 기존단계와 ‘복원완료’단계를 분리한다.

필수 대조: A실제present→Bdecode/ACK만성공·Bpresent실패→exit후A복원, Bpresent성공뒤에는B복원, stalegeneration/monitorlist늦은응답이보류대상을확정하지않음, 실제render실패시Done/새descriptor보고0. 부정대조는 ‘present대신decode로확정’복원시 실패해야 한다.

## D3 — B2/B3: 저장소의 거부된 미래 기록 재사용과 최초 lock생성 경합 (필수 보수)

1. `RecordWedge:382`는 ParseRecord만통과한기존값을 넘기고 `NextRecord:254~258`는first<=now이면 `last=max(existing.last,now)`를유지한다. **first는과거/last는허용skew를넘는미래**인기록은 startup에서refused=future인데 실제새wedge가나도그last를재사용한다. 이후startup도여전히그기록을거부하므로 **새실제wedge의영속격리가작동하지않는다**. 정상50초rollback의단조유지는현53개시험에있지만이거부된기록수선반례는없다. 허용skew밖의기존future/시간오염값은계속되는이력으로재사용하지말고, 신뢰파일안의손상기록을새실제관측(now)으로다시시작한다. 쓰기실패시이번run WGC유지는그대로다.
2. `FolderLock::Acquire:116`이새파일을만드는`create_new_with`는 **share=0**이다. 최초lockfile생성자가파일을잡은동안다른supervisor의open/share-RW가sharing violation로즉시실패하고, 현재Acquire의3초대기는LockFileEx단계의LOCK_VIOLATION만재시도한다. 현32wedge동시시험은lockfile을미리만든뒤여서이구간을다루지않는다. **빈보호폴더에서처음동시에2writer**가들어오는경우도공유가능한생성handle또는bounded open/retry로직렬화하고정상경합의기록을잃지않게한다. ACL/owner/NOFOLLOW검사와총deadline은유지한다.

추가대조는미래기록→실제wedge→다음supervisor WGC, 완전빈폴더·서로다른process/writer동시최초생성→둘관측보존과상한시간을검증한다. 원복mutant가실패해야하는반례다. 정상locktimeout·거부된링크를보안완화로통과시키지않는다.

## D4 — B1 시험 빌드: 재생성 장치의 MT 적용 순서 (시험 범위 보수)

초기`startup_init_graphics`는MT→MF순서이고off는관찰만이므로OK. 그러나`RecreateCaptureDeviceOnPrimary`는 `encoder.codec.set_d3d11_device(res.d3d.Get())`(115) **뒤에** `mt_ab_apply`(117)를호출한다. ‘장치생성직후·MF/readback/WGC/DXGI가쓰기전’이라는시험계약/실기문서와다르다. 재생성된newcontext를다른소비자에공개하기전MTarm을적용하고Get으로기록한다. off를강제OFF로만들지않는현재정의는유지한다. 시험빌드의초기/재생성호출순서를관측하고출하표식부재를같은후보에서확인한다. **MT출하정책은여전히보류**이며이수정은제품기본MT를켜라는승인이아니다.

## 계약별 판정 및 EVIDENCE

| 계약 | 현재 판정 |
|---|---|
| B1 MT A/B | test-only분리/초기순서·Get확인OK, D4보수;NVIDIA인과/출하반영보류 |
| B2 저장위치/권한 | known-folder·SY/BA·parent-held·reparse/hardlink거부·CREATE_NEW/handle교체확인;D3최초경합보수 |
| B3 내용/만료/실패 | schema/크기/16개/driver/7일·이번run유지구현확인;D3future수선보수 |
| B4 prelogin | SOURCE OK: 격리적용제외, DXGI전용계약유지 |
| B5 격리우선 | SOURCE/EVIDENCE 해당범위OK: EffectiveRequested를startup/전환/선택/reattach/promotion에사용;별도GPU16/15대조확인. 실제field wedge원인해결주장아님 |
| B6 재연결복원 | **SOURCE NEEDS_CHANGES D1/D2**, 기존사용자취소/새host/operation분리·새connect경유·3회budget유지는확인 |
| B7 행동검증 | **E1 보완필요**: 보수후고정UI사슬·반례독립검사,승격ProgramData/재부팅/NVIDIA는별도미검증 |
| B8 진단 | snapshot非동기·제목미수집·중복요청거부기본구현/시험확인,방화벽영향기록유지. minidump보류는승인된범위,새게시관문아님 |

**E1:** client_recovery_ui_test/전체사슬은이번독립검증에서SKIP였으므로acceptance1의동작은작업용귀속이다. D1/D2보수후 UI슬롯이가능해지면검증용이**고정후보로독립사슬실행**, ‘재연결중→클릭없이정확한대상의새present’와반례를확인한다. 기존stand-in shell시험/단순26PASS가이것을대신하지않는다. 영향없는GPU전량반복은요구하지않고store/restore/MT보완영향범위와출하gate·artifacthash를다시고정한다. 남은실제NVIDIA/release phase/승격ProgramData/재부팅은사용자실기묶음으로준비하고미관측으로남긴다.

## 비차단 한계·기록

- present-adapter가하나라도기록과맞으면현재worker전체WGC로시작한다. 동일모델2장key공유뿐아니라서로다른GPU도‘기록GPU가present’인동안다른선택GPU의DXGI를보수적으로제한할수있다. 장치별선택격리를입증했다고하지않는다. 이보수적성능한계는문서에추가하되새아키텍처확대를요구하지않는다.
- 표준diff-check는CRLF와CMake끝빈줄을보고했고CR허용시CMake4158의EOF빈줄1건만남는다. 최종관련편집때정리하면되며이형식문제를D1/D2와같은결함으로세지않는다.
- staging정리WARN은시험결과실패로바꾸지않지만남은경로/원인미확인을유지한다. 첫fixture의방화벽Allow Inbound Query User규칙은원본실행영향으로사용자안내를유지하고Codex가임의삭제하지않는다. 현loopback수정은확인대상에포함한다.
- MT실기문서의ProgramData폴더비우기/설치exe교체는**사용자와정확한범위를조율한수동절차**다. 지금실행하지않고관리자삭제권한의일반허가로읽지않는다. minidump·Bⓑ2·MT출하는보류유지다.

검증용은기존t-dreqrszk에D1~D4/E1을통합해작업용2로보수연결한다(SH보다DW보수가우선이라는현재정본규칙). 같은기능새task·매commit중계·전량테스트를추가하지않는다. Codex직접은소스/diff·원본로그/해시·작업용화면증거열람만이며제품편집/빌드/설치/서비스·UI조작/실기/게시를하지않았다. 이미추적판정서외변경/별도commit/push없음.
