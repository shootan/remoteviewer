# Codex clear 재개 메모 — 2026-10-22

## 사용자 지시
- **LK-UX SOURCE r3 9ff6c15 재확인**: 598339a..9ff6c15 제품/testdiff및호출부읽음/HEAD9ff6c15clean확인, `.claude/codex_lk_ux_retry_src_r3.md`작성·검증용pin. **N2-a/b닫음, N1취소뒤Write개시gate닫음**. 최종SOURCE는**N1전체시한2곳잔여NEEDS_CHANGES**: Exchange171 now+사전상대budget가gate.absoluteEnd로clamp안돼deadline직전send후readwait가end초과가능→minend공유; 620~636예비inTime와639~656실제hold/cache가별도mu라250ms여유중end지난발급채택가능→최종commitmu안live+deadline동시검사. 좁은2반례/2위치보완만요구·이미닫은N2/쓰기gate재설계없음·전량UI/GPU지금재실행불요. worker81×3/viewer5/68×2/79/28/M7M8M9는인용, EVIDENCE통합후속. PSUA/SH독립진행가능·제품/서비스/UAC/commit조작없음.
- **LK-UX SOURCE r2 598339a 재검토**: 9991b36..598339a만diff+호출경로읽음/HEAD598339a clean확인, `.claude/codex_lk_ux_retry_src_r2.md` **NEEDS_CHANGES 잔여취합**·검증용pin. N1opdeadline/queue회수등부분수용하나postfence~Write race/expiredleft=1새budget·hold채택deadline미검사남음→I/O개시선형화/finalfence·Write직전latch회수반례. N2로컬봉인전실패/17보존닫힘. **N2-a** Bseal송신가능unknown시B소유ID도버려짐(정상pending40×250ms~10s vsrelaysealed25s→B늦은typed후다음88pending거절재발)→Btentative상태/status/다음retryB대상유지. **N2-b** challengeIssued표식purpose검사전/17도purpose검사전return→검증뒤trust표식·틀린purpose대조. 새시험실행없음·worker70×5/62/79/28/M5M6는인용/EVIDENCE는통합후속. 새REMOTE60_VIEWER_UNLOCK_TEST_SEAM은e2etargetonly소스방향수용·통합define/bytegate합집합확인필수. 기존LKtask좁은보수후새diff·PSUA/SH독립진행유지·제품설치/UAC조작/새중복task없음.
- **LK-UX SOURCE 9991b36 검토결과**: 검증용요청source-only수행, `.claude/codex_lk_ux_retry_src_r1.md` **NEEDS_CHANGES N1(P1)/N2(P2)** 작성·remote#yj8mrmrx pin회신. N1 RetrySync828~829 caller timeout이liveTx/queuedWorker취소안해18뒤late supersede/freshchallenge/hold가능→op전체absoluteDeadline/cancel/IPC실행권fence·큐지연/lateack/EndSession반례. N2 oldA종료+BChallengeIssued확인뒤로컬봉인등실패stage0→A계속poll→old17을15(2분미확인)로오안내→priorEnded와B결과분리/봉인전실패반례. 정적interleave판정·새실행재현주장아님. 정상소유자cookie/request/heldtxGen·serialservice·freshchallenge/oldseal·capability조합수용, UIA실패감지보류수용. 번호89/0x80000/17/18/IPC9/feature2검증용확정수용·UA2예약90/0x100000통합충돌검사. HEAD9991b36/clean직접확인, 제품diff17읽음/보고전문. EVIDENCE는R1통합독립1회고정해시에후속·현재독립없음이source결함아님. 기존LKtask에N1/N2취합보수후새diffSOURCE확인·PSUA/SH독립계속·전량UI/GPU지금재실행요구없음. Codex제품수정/빌드/설치/서비스/commit없음.
- **10-26 1·2순위 재개·게시(push포함) 승인 / rel-0163 설계확정**: 받은task **t-nzdilv86**, `plan_rel_0163_2026-10-26.md`전문·PS실기원본·제품소스검토→`.claude/codex_rel_0163_r1.md`작성. 해당R1/R2범위일시중지해제, 반복게시go불요·다른무관과제까지자동재개아님. **UA2 GO**=인증논리session cookie/epoch소유override·옛종료/명령새설정무간섭·resume/pause유지·새APK전역desktop_backend자동재적용중단/요청vs실제상태협상표시. **PSA조건GO**=기존BootstrapFrameCache는flush후존재하지만KickTryFill nonDefault거부/streamgen조건을sourcefence로해결, immutable소유원본·desktop/session/attachment/target/size무효화·옛캐시유출금지·첫AU없어도bootstrap·없으면유한dup재획득. 현재staticdefault는1000ms=1Hz(계획현2fps정정), **R1active정지갱신목표500ms=2fps 채택**, parked/비활성영상0유지. **PSB GO** prelogin재부착DXGI우선/실패만r10T2종료. **UA1 GO** 같은연결desktopedge감지/WGC재구성/oldcache세대·IDR fence, 실제lock판정분리. **KA전면유지초기기본GO** 기존수명유지설계선택/미답동의간주아님·background완료주장금지, oldcodex_keepalive_r2머리§5최신판정링크갱신.
- 배정수용: 작업용1 LK5b1d2e0마무리→PS+UA→R1통합후KA328d616spike, 작업용2SH→UA2APK→KA6d85ce2→NP설계후구현. 위3WIP존재/parent만직접확인·제품OK아님. R1 .163/.32=1순위+SHA, SHB기준통과별도선택; R2 .164/.33=KA+NP, NP설계선행/공유기변경묵시승인없음. 새구현정본/실taskID는검증용연결, 새PSUA최초task필요·LKSH기존재사용. 검증용독립→Codex고정후보검토→서명/배포/외부hash→main/docs일반push/dist/최종실기대기 안내; 사용자기기자동설치·UAC승인·재부팅없음. Codex제품수정/빌드/설치/삭제/게시/commit없음.
- **10-26 UAC 설정원점 보완(검증용인용)**: remote#yj8mrmrx evidence수신·기존판정서에반영. NAS APK10-22 15:24:17설정탭→21.768dxgi→23.217wgc요청, 그전syncDXGI/그후10-22·23·25WGC라는검증용판독. PCViewer는backend요청없어폰WGC선택이Host전역requested에남아PC접속에도유지. 15:13~16잠금시험은인접시각일뿐원인아님. Codex기존직접19:16APKsync/Hostapplied와연결, 새15:24전체기간원문은이번에재다운로드하지않음. 검증용이사용자에게DXGI복원후재접속임시우회이미안내했다고보고(성공미검증); Codex기기설정/제품/배포변경없음·paused과제재개없음. 중복A2A ACK/사용자실기재요청불요.
- **10-23 UAC 재현·원인조사 최종갱신**: 사용자카카오실행→UAC안보임→첫재접속UAC보임→예→검정→둘째재접속복구. KakaoTalk실프로세스18:07:23 생성·로컬Host/NASViewer/입력로그직접연결. Host18:07:15.904/17.422 첫재접속WGC재생성(stream20), 18:07:33.418/34.271 둘째재접속WGC재생성(stream21). UAC입력454/454전달·18:07:26 callback20/encoded19/sent19·viewer9032새seq계속present. **문제지점=WGC동일backend유지상태에서UAC입력desktop진입/복귀시캡처자원갱신누락**, 네트워크/권한상실/잠금오인으로영상차단하는규칙아님. `host_stage_backend:251` desktopprobe가active!=EffectiveRequested때만실행→requested=active=WGC는edge추적빠짐. capture_session:599새item/pool/session만들면재접속시복구. `.claude/codex_uac_prompt_check_2026-10-23.md`상단최종판정및test-tmp/uac-investigate-2026-10-23 증거참조. **WGC UAC절대불가/SYSTEMhelper부재가이번원인이라는초기추정폐기**(과거.103WGC UAC실기+이번freshWGC UAC성공). fixedpatch A/B/Windows내부어느자원이black인지/도입commit은미확정. 방향=같은연결desktopedge감지→WGC재구성/세대fence·IDR, LockCred보안경계보존. 현제품수정/빌드/설치/서비스/보안설정/UAC승인없음·다른과제paused유지·타세션온콜OFF에지시없음. 파일/시각질문은사용자추가재현+CIM시각으로해소.
- **10-23 UAC제보 사용자정정으로대상확정**: 관리자필요파일실행때의 **UAC 예/아니오 보안화면이원격뷰어에안보임**이다. Host자체승격질문아님·기존트리거질문답대기해소/중복질문금지. `.claude/codex_uac_prompt_check_2026-10-23.md`상단정정판독참조. 현재WGC사용(10-22 19:16 requested=wgc, 10-23계속reattach), quarantine0/storeempty→DXGI격리탓단정금지. user-admincapture와SYSTEMpreloginworker는별도, 일반WGC→UAC securecapture연결누락가능성우선조사·실UAC전이재현/원인commit미확정. 16:45조회Default/consent없음. Host권한정상은이회귀배제근거아님. 과거#234클릭/DPI문제와이번표시누락구분. 현재Codex주소remote#18s8ffjf/검증용remote#yj8mrmrx·작업용1/2모두온콜OFF(새도구조회), 옛sessionpin/CLI폴백/paused과제재개없음. 제품/서비스/설치본/보안설정/실UAC조작없음.
- **2026-10-23 새 조사만 재개 / 기존 일시중지 유지**: 전날 사용자 '모든작업일시중지'가 KA/LK-UX/SH 전체에유효. 새 사용자 '관리자확인창갑자기안뜸·로그인관련회귀확인'에대한읽기전용조사만진행. `.claude/codex_uac_prompt_check_2026-10-23.md` 참조. UAC켜짐1/5/1, 서비스4804→Host9864→Stream2456, Host/Stream실token elevated1/full2·Explorer2204limited3 확인. 설치3exe는보존된공개0.2.162외부hashJSON과일치. 서비스로그인후Host자동승격실행소스경로는0.2.162변경없음/10-07~08기존동작. **권한상실회귀관찰없음**, 단사용자가어느창트리거말한지async질문답대기이므로원격UAC캡처회귀배제/정상종결아님. 보호workerlogAccessDenied→ACL/UAC우회없음. 제품/서비스재시작·설치/레지스트리/실UI무조작, 다른세션메시지·작업재개없음. 다음정확한동작/PC/시각답에따라추가조사.
- **SH r2 최초 실행 연결 확인**: `.claude/task_sh_strength_r2.md` 전문읽음/§4·§4B구속계약 일치. 작업용2 remote#sei64muv **t-6y8i0w1t**, keepalive APK준비 t-x27113l6 보고뒤착수지시(main82b3d14 새branch; MainActivity/bridge등동시writer없게순서조정). report_sh_strength_r2.md→검증용독립→Codex A/B별판정. 메뉴로그만정상근거로끝내지않고 actual.35uniform/최종픽셀·sameframeA.35↔B.35/강.60·Host좌표/부정대조/비용필수 명시확인. 제출/착수/완료실행증거는아직별도미수신·중복ACK/재task발행없음. 현재게시본/폰자동설치불변.
- **SH 사용자스크린샷 근거검토완료**: 두ChatGPT JPEG2048×945(`temp/...03_49_56.jpg`GNLink약.20/`...03_50_12.jpg`OSLink)+확대crop직접열람, OSLink획더밝고굵고읽기쉬운관찰수용. `.claude/codex_field_0162_0231.md`§4B최소비교행렬앞증거추가. 강도/viewport4%내외크기·시각·재압축이력미통제라원인/B우위증거아님, 같은JPEG치수로동일처리증명못함·배경32로전체색동일성못증명·p90/p99등ROI/threshold미제공수치는검증용인용. **후속범위이미확정=A강.60+B표시크기1pass+같은.35대조**; 패널폭/폰트bold/색변환별건추가없음. 검증용에SH유효정본취합→여유작업용2최초taskID연결요청전달, 재설계go/스크린샷중복질문불요. 현게시물불변/제품채택·출하OK아님.
- **field후속SH design-delta판정**: `.claude/codex_field_0162_0231.md` **§4B**가추가구속계약. 강.60 + 표시크기확대/선명화동시1pass **A/B시험GO**(S1원본출력제약B실험에한해완화·채택/출하OK아님), A.35vsB.35필수·.60분리. q8원자료/현renderer/ViewerViewport직접읽음: 무clamp선형필터/보간순서는교환가능해후단bilinear무조건원인단정금지, 중간clamp/양자화/색/source가설검증. B=실view픽셀출력·기존Viewport placement GL1회/TextureViewidentity, source/out/view분리·source1px taps·crop/회전1회·ringmissoutputfallback금지·정지panredraw/크기세대·singleproducer/BLOCKED/keepaliveowner·off복귀보존. 최종sameframe/터치Host대조·부정대조·p95/miss/발열후채택검토. OSLink비교스크린샷검증용이미요청/중복요청금지. 작업용2여유시별도branch·유효SH정본+최초taskID대기(아직미배정).
- **LK-UX 실제 실행ID확인**: 버스목록조회로 작업용1 remote#rqxbj3jb **t-wfv0aneu**/submitted 확인, `.claude/task_lk_ux_retry_r1.md`전문codex계약일치. hostspike t-y1lnbgnd는WIP보존후대기하라는지시(실제pause완료관찰아님). 명시retry우선·UIA조건부/다른Busy/noncefresh/평문금지/원본자료보존유지. 다음report_lk_ux_retry_r1→검증용독립→Codex. 작업용1에직접별도지시/중복task발행없음.
- **새 실기 후속 field-0162-0231**: `.claude/field_0162_0231_2026-10-22.md` 전문/source·로컬secure_input.log확인, `.claude/codex_field_0162_0231.md` 작성·검증용pin. 잠금해제15:13:23.693/15:16:44.168 실제성공2건, req3 TargetNotReady13입력0/req4틀린암호뒤대기UX결함. 120초는코드상한(재접속까지약75초)·TargetNotReady포커스원인단정금지. **LK-UX보수GO**: 같은인증cookie+request+txGen+LockCred소유자 명시retry→옛대기cancel/commit차단확정→freshchallenge직렬교체, oldsealed멱등/다른연결Busy/최종LogonUIfence유지. UIA실패감지조건부·freshattempt/SYSTEMWinlogon신뢰/bounded비밀없는관찰, 미지원에도reconnect없는retry필수·판정불가를암호틀림으로표시금지. UIA만보류가능.
- SH field후속=약.20/default/off유지·강.60비교후보GO, 로그는최종폰픽셀증명아님→sameframe기존.35/새.60·halo/클리핑/스크롤검증/폰체감별도. .25약변경/단계추가불요. reconnect punch2503/hello3는검증용NAS발췌귀속·NP분리(즉시NP구현GO아님)/kindpublic만relay단정금지. **검증용이LK-UX우선·SH별도유효정본/실taskID연결(아직미수신)**, KA t-y1lnbgnd/t-x27113l6중복writer/중복지휘금지. 현재게시물불변/버전은준비된묶음릴리스때확정. Codex제품변경/빌드/기기실행없음.
- **KA-r2 최초 실행 연결 확인 완료**: `.claude/task_keepalive_r2.md` 전문 확인. A Host/admission 격리 spike = 작업용1 remote#rqxbj3jb **t-y1lnbgnd**, 보고 `report_keepalive_r2_host_spike.md`; B APK 재통합/정책 준비 = 작업용2 remote#sei64muv **t-x27113l6**, 보고 `report_keepalive_r2_apk_prep.md`. A확정 전 실제Host자동preconnect 배선OFF, sharedcontrol/Host권한·수명 A단일writer, B는독립클라이언트준비. ②~⑤는A Codex확정후. 정본이codex판정구속·백그라운드답대기·원본자료보존·현버전/설치본무조작을명시해설계차이없음. 답장불필요로ACK재송신없음·delivered를착수/완료로주장안함. 다음각보고→검증용독립→Codex, 사용자폰백그라운드질문은기존1건유지.
- **최신 새 과제 keepalive-r2**(10-22): 사용자 '가장 마지막 접속 기록 있는 host pc 최대3대, 켜져있는동안 연결상시유지, apk도pc클라도'. `.claude/proposal_keepalive_r2_2026-10-22.md` 전문읽고 `.claude/codex_keepalive_r2.md` 작성·검증용 pin 회신. **수정조건부 설계GO**, 구현/출하OK아님. 기존3대·5분미결 중 3대확정/5분만료제거·시작preconnect수용. 폰홈/화면꺼짐포함은 **Codex request_user_input_async로이미질문/답대기**, 중복질문금지·답없이onStop종료최종확정금지. 독립Host/세션재통합계속.
- KA-r2 핵심: 현Host SessionState단일peer/channel/epoch + HelloAdmit새peer교체라 **pause만으로APK+PC동일host상시공존불가/자동탈취위험**. 복수인증대기entry/active1/명시activation·대기heartbeat무권한/relay식별·구Host협상계약의제한spike먼저 검증용→작업용 최초task연결(아직ID미수신). 자동Hello기반3대retry출하금지. APK actualmergebase eedd207→a2e9686 apps26(native13)을현재main82b3d14에3way전진, SH/hello등보존. 기존never_shown10s/Active전용BeginPark도수정대상. PC=host별managedViewer유지합계3/기존다중가시창보존·privateIPC·entryreconnect·lazy미디어·park억제·셸종료/crash회수. 상세계약/비용/행동검증정본참조. APK.32/vc31·PC.163후보·현재게시본불변. Codex제품수정/빌드없음.
- **“내 명령 기다리지 말고 A2A 오면 알아서 진행해.”** 일반 A2A·보관 메시지 처리에 매번 사용자 확인을 요구하지 않는다. GMux의 자동 보관 안내를 이유로 다시 승인 대기하지 않는다. 사용자 직접 중지/새 권한 경계는 별개다.
- 제품 작업은 검증용 remote_claude가 작업용 지휘·독립 검사를, Codex는 설계/소스/근거 검토를 담당한다. 기존 task의 같은 범위를 중복 위임하지 않는다.
- 받은 A2A 답장은 reply_to의 identity+sessionId로 pin. 새 세션은 a2a_whoami/a2a_agents로 확인한다. 일시 주소를 재사용하거나 identity만으로 fan-out하지 않는다. 상대가 버스에 있는 동안 codex CLI 폴백 금지.
- 사용 중인 설치본/서비스·로그아웃·재부팅·UI 입력은 임의 조작하지 않는다. **최신 사용자 지시는 PC0.2.162 게시 + SH포함 APK0.2.31/vc30 함께 게시**(A2A user-directive 원문수신, rel-apk seq13401). 버전/SH포함·서명/NAS게시 보류는 이 범위에서 해소됐고 게시후실기 경로다. 기기자동설치허가는 아니다. keepalive-multi 정책값/최종출하와휴대폰sealed잠금입력은미결/미구현으로제외한다. **git push도이번게시의검토된main+관련릴리스기록일반push범위허용**: seq13402원문근거/state_2026-09-30.md(43f5c45주변) 사용자10-01'게시=push'·handoff10-21prompt:14/17직접확인해보류해소. force/원격삭제/무관변경push는허가아님. 같은승인/원문확인재요청금지.

## 최신 게시 상태 — PC0.2.162 + SH APK0.2.31 게시 완료 / 실기 대기
- **최종 문서 정리까지 닫음**: `82b3d143f89a9e0ceb1c1886ac72e22022e3f46c` main/origin 직접 ls-remote 일치. history만+2/−2, 양쪽본문보존·잔존구분자제거·APK공유punch/hello와PC전용목록요청구분·정정기록. merge표식0/diffcheckrc0 직접확인. 아래seq13429잔여/inbox13430전문대기는해소(전문도착읽음). 제품/게시파일변화없음·추가자동작업없음. PC/APK 배포마일스톤및기록완료, 실기/별도ledger대기; 전체미게시과제완료아님.
- 최종 문서상태 직접확인: **main/origin `7d8c418c68447518e0d2ef7a6c943b37742a2ecc`**(SH 작업목록/계획checkbox/history 정정, 제품 불변). 두 채널 게시검증은 완료했고 Codex 최종 사용자 안내 진행. 추가 이력정정 **seq13429**(history13438행 잔존=======제거·APK후보의DW PC전용목록요청 공유문구정정)은 아직7d8c418에없어 검증용 후속. **inbox13430 inject 성공/전문아직미주입**: 다음턴 전문읽고 최신commit/push 확인, preview만으로완료추정/중복요청금지. 제품 재검사/게시go 불필요.
- **APK0.2.31/vc30 게시 완료**: task `t-en2shyuh`, build `2b57bb8863ebe3e752a185bbf461dc5421e9c1b8`, main/origin `d41162ed636e336e796487b784a3051b5d06c16c` 직접 ls-remote 확인. 아래 'APK 준비 중/실제taskID대기/Android0.2.30'은 이전 PC단계 기록이며 현재상태 아님.
- `.claude/codex_rel_apk_0231_final.md`: 공개 APK 200·13,799,096 B·SHA256 `230a65e99a2a7ee30ce0af978e51eb0fa189f457e9a07dfddb4dc69484a9d5d4`·dist 전체바이트 일치, apksigner V2 전체인증서0.2.30과같음 `dcc806ae…c2990`, aapt vc30/0.2.31·4ABI, gate7·실제4ABIcompile 시험define0, PC95+SH22 apps disjoint/원blob보존 직접확인.
- JS서명·변조거부 직접실행/SSH 서버 Android pair `309a9c5e…/cbb5b7d6…`로컬일치 version0.2.31, Windows pair/0.2.162 보존. 독립AVD 원문monitors13/compare25/one12PASS1FAIL 직접확인·off/strong PNG열람. Desktop탭+카드오집계 하네스기대값오류 설명수용/rc1유지·실dump미보존한계. S4AVD미달·폰전부/기존control원장미해결·KM제외·폰sealed미구현유지.
- **문서 소규모 정정만 잔여**: 공개작업목록SH행 '결정대기/CAS확대'를 실제0.2.31게시·5탭·실기대기로정정, 구현계획SHcheckbox상태만/history취합→검증용 GitMCP문서commit+일반push→실remotehash 전달. 제품보수/재빌드/반복go불필요. Codex게시최종사용자보고에실폰메뉴끄기/.20/.35 비교·PC OS/회사PC 실기잔여 분리.
- PC **0.2.162 배포 완료(마일스톤) · 실기 테스트 대기**. build `b4dfaedfa08e2023a495154fc59e7bda66272cbb`, main/origin `9b46462267c11f4984522d2fcd295b280e52ffc2`. Codex가 실제 `git ls-remote` 확인. `.claude/codex_rel_0162_final.md` 참조.
- Codex 직접 출하 verifier 정상/변조 거부·JS 서명 확인, 서버 Windows manifest/sig hash 로컬 일치, **공개 HTTPS11개 크기/hash 전부 일치**. manifest `814e4bb9…`, sig `a39b2f14…`, dist Setup `ed480e25…`. Setup Authenticode 미서명(업데이트 manifest 서명과 별개).
- 서버 직접 확인 시 Android는 **0.2.30**, pair `b64c1b49…/210ab909…` 유지. **SH 포함0.2.31/vc30은 아직 준비 중**(기존 작업용2 task; 실제 taskID/고정 산출물 보고 대기). 검증용 FINAL→Codex 근거 확인·최종 사용자 보고까지 이어간다. 전 범위 완료로 닫지 않는다.
- PC LK/DW/hello 자동검증/통합 검토 완료와 OS 실기 A~D·회사 PC/NVIDIA·실 LAN/NAT는 구분. 명시 monitor/window 재선택 필요, 새 Host에서 휴대폰 잠금 암호 입력 불가(0.2.31에도 sealed 미구현). SH S4 AVD 미달·실폰 화질/성능/발열 및 control ledger 미해결 유지. 기기 자동 설치/재시작하지 않는다.
- 아래는 과거 진행 기록이며 최신 권한/상태는 위 항목이 우선한다. 게시=이번 관련 main/docs 일반push 허용 근거 확인 완료; 같은 질문 재요청 금지.

## 이전 게시 기준선 — 0.2.161
- PC **0.2.161 배포 완료(마일스톤), 사용자 실기 진행 중**. build `6be579b`, main/origin-main 기록 `5063cd9ae4269c93c99c4ecc69600f353481d624`.
- 0.2.161: 트레이 TaskbarCreated/재시도, prelogin WGC 탐지/WinRT wrapper 생략(원인 확정 아닌 방어), 단계/HRESULT 진단, SY/BA 전용 worker 로그·회전·bounded reader/queue/writer·설치 rollback.
- Codex가 게시 manifest 서명/변조 거부·HTTPS11파일 hash·Android pair 불변을 직접 확인했다. manifest `a6584fa8…`, sig `46fe7e15…`. dist/GNLinkSetup-0.2.161.exe SHA256 `4b6a7fa468c250ee3d937a9fc8c68484f854f8b998e17cccce25251e4c54453e`.
- 검토 기록: `.claude/codex_prelogin_field_d3.md`, `codex_prelogin_field_r2.md`(NEEDS_CHANGES), `codex_prelogin_field_r3.md`(dec6006 SOURCE/EVIDENCE OK). 이전 0.2.160 통합/구형 APK 판정도 `.claude/codex_prelogin_*.md`에 있다.

## 현재 본래 작업 — LK/DW 구현 연결 완료, 제품 결과 미완료
- **최신 구속 설계는 `.claude/codex_lockkeys_dxgi_r1.md`**. 요청 `codex_req_lockkeys_dxgi_r1.md`, 조사 `report_lockscreen_keys_r1.md`·`verify_lockscreen_keys_r1.md`·`report_dxgi_wedge_first_session_r1.md`는 읽고 판정 완료. 같은 조사/구현을 중복 위임하지 않는다.
- 앞선 “뷰어 키 송신0”은 집계 착시로 정정: NAS 확인 PhysicalKey42건·IME activated, 호스트 non-Default PhysicalKey 거부(코드), 기존 disconnected unlock RejectedPolicy10회·결과 stdout만·저장 암호 재사용, 버튼 prelogin 전용. Windows Update ARSO 자동 로그온+잠금 근거가 있고 Security logon type은 미확인.
- **A: `.claude/task_lockscreen_cred_impl_r1.md` → 작업용1, 구현 task `t-rs9nhsdv`**. 잠금/prelogin 공통 버튼·매번 prompt·무저장·결과창, 신규 LockCred purpose와 service/agent 경계, host와 최종 secure 경계의 잠금 평문 문자 차단 포함. UAC와 구분. **APK0.2.30 유지로 새 호스트 잠금 화면의 휴대폰 비밀번호 입력 불가**를 호환성 영향으로 검증·최종 안내해야 한다. 구형 APK를 위해 평문 경로를 남기는 원안은 불채택.
- **B: `.claude/task_dxgi_wedge_fix_impl_r1.md` → 작업용2, 구현 task `t-dreqrszk`**. ProgramData SY/BA 장치/driver별7일 WGC 격리 영속화·모든 backend 적용 경계 격리 우선·재연결 desktop/monitor 복원·제한 진단. **prelogin WGC 적용 제외**. MT 변경은 test-only A/B만/출하 보류, directory 신호 Bⓑ2 제외. dump는 별도 helper·5초 복구 불변·private bounded local-only 증거가 없으면 그 부분만 보류.
- 검증용의 delegation-linked 수신 뒤 두 지시서를 끝까지 읽어 설계와 일치 확인. 버스 직접 조회 시 두 구현 task는 **submitted**였다(착수/완료로 바꾸어 주장하지 않음). 다음: 작업용1/2 구현 보고 → 검증용 독립 검사 → 통합 PC0.2.162 고정 후보 Codex 최종 검토. 보고 예상 경로 `report_lockscreen_cred_impl_r1.md`, `report_dxgi_wedge_fix_impl_r1.md`.
- A 계약4 추가 판정 `.claude/codex_lockcred_agent_r1.md`(seq13181): one-shot SYSTEM --lockcred-agent는 허용하지만 **Session0→console 익명 파이프 상속은 공식 API 제약 때문에 NEEDS_CHANGES**. 검증용에 service-owned 일회 named pipe(SY전용·first-instance·remote거부), FALSE inheritance·suspended/job/resume, held child/session+trusted service 양방향 인증 후 전용 bounded IPC로 진행하도록 회신. 기존 start_agent(TRUE) 코드를 새 credential 경계의 보장 근거로 삼지 않는다. argv/env/file/log 비밀 금지, timeout/cancel/child회수, 실제 세션간 실행 근거 필요. 번호0x40000/control88/kind5~8/stage13~15 이견 없음. 다른 A구현 계속, 기존 task에 통합하며 중복 위임 없음.
- A Q1/Q2 판정 `.claude/codex_lockcred_q12_r1.md` 회신seq13193: 미결job중복거부+(승인retry의 동일검증LogonUI암호칸 교체) 둘다OK. 일반nav/plaintext확장·암호읽기·부분실패뒤append/자동다른방식재시도 금지. timeout/cancel뒤재시도 가능. capability조회는응답후즉시close/backoff, 실제challenge→seal→status는 per-connection바인딩보존하고 deadline/terminal/cancel/오류정리후반환. idle잠금HostA probe뒤HostB unlock대조필수. 현재9956cca 계열 일부source읽기이며 전체후보독립승인/실기PASS 아님. 앞선one-shotIPC계약도유효.
- 10-22 당시 검증용 `remote_claude` remote#wwml7h91, 작업용1 remote#rqxbj3jb, 작업용2 remote#sei64muv, Codex remote#am104clp 모두 onCall. **재개 시 a2a_agents/whoami로 다시 확인**, 받은 reply_to identity+sessionId로 pin. 이 기록의 주소만 믿어 재전송하지 않는다.

## 별건 문서 — oslink-docs-2026-10
- 검증용이 main5063cd9에서 공개 문서4개 작성 후 전문 검토 요청. `.claude/codex_oslink_docs_r1.md`에 **NEEDS_CHANGES R1~R6** 작성·검증용 pin 회신(seq13174).
- 핵심: 특정 루프의 hang 감시 미확인→제품 부재 확대 금지, 로컬 SendInput≠네트워크 평문, raw IPC/인코더 위치와4:2:0 추정 한계, 논리 stream≠SDK 인코더 동일성, LK/DW 사실/설계 완료 상태 정합, 구현계획은 status만. encoded-AU 복구 방향은8월ADR과 일치, 후보 문서 코드 본문 복제 없음. **CS는 작업목록 등재이며 제품 구현 착수 승인 아님.**
- r2 후보5파일(hash 앞12: 신규cfe21e5cfede·기존b1d27cc3a979·작업목록578f71772b4d·history69651c1038f7·구현계획57fbc3317f0d)를 직접 읽고 **R1~R6 종결 OK**. `.claude/codex_oslink_docs_r2.md` 작성·검증용 pin 회신(seq13177). 커밋 직전 전문검토 완료 체크/history 실제 결과 metadata만 갱신하는 delta는 허용했고 이 때문에 재검토를 반복하지 않는다.
- **문서 commit `c04eb47bf1fdade3065b155cede8cc3c210d10ee` 직접 확인 완료**: 정확히5문서만, 첫3개 후보hash 불변, history/구현계획은 허용한 r2 OK 상태metadata만 변경, CR-at-EOL허용 diff-check exit0, tracked clean. 검증용 FINAL_PACKET도 수신해 직접 확인값과 일치·Git MCP commit·push 미실행(origin/main5063cd9)을 확인했다. 사용자 최종 보고 완료. 제품 A/B를 이 문서 때문에 중단하지 않는다. Codex가 공개 문서를 직접 고치거나 별도 commit하지 않았다.

## 추가 사용자 작업 — SH APK 선명화 필터
- **최신 상태: 사용자 명시 RESUME, 구현task연결 완료.** 정본 `.claude/task_sh_filter_impl_r1.md` → 작업용2 remote#sei64muv / **t-b7ss11ov**(직접조회submitted). ‘0.2·0.35둘다선택’ 사용자결정으로기존hold해제. 정본과 `C:/work/oslink-analysis/android/q8_sharpen_renderer.md`전체를직접읽고델타검토. 이전hold가자동재개된것아님.
- 요청 `.claude/proposal_sh_filter_r1.md` S1~S4를 소스/API 대조하고 **`.claude/codex_sh_filter_r1.md`로 보완 계약 확정/회신(seq13186)**. visible 원본 해상도1패스·기존Viewport/터치 유지, 끄기/약/강·기본약, off=기존direct. off↔GL/fallback은 codec reset/IDR rearm을 포함한 직렬 producer 전환, 약↔강만 uniform. 현재selection/surface generation의 최종TextureView latch 확인·crop/OES샘플경계·GLthread수명·실제direct복귀가 필요.
- 지연p95+4ms는 codec→swap만으로 판정하지 않고 codec handoff→최종latch 매칭 A/B. 선택30/45/60fps와 같은입력·장치·crop·색·열/반복수명 대조. AVD와 SM-S948N 실기층 구분. 실제선명도/성능/실기PASS는 아직 없음.
- 재개델타: CAS대신 자체5탭십자 `(1+4s)C-s(L+R+T+B)` RGB·alpha1·clamp, off/약0.20/강0.35 값표시·기본0.20. 구속판정서 계약2와머리상태/다음담당을직접갱신해옛CAS/hold충돌해소. 소스1pixel·crop/회전검사 등S1~S4유지. q8의OSLink최종viewport선명화와GNLink원본pass후확대는다르므로동일화질보장/CAS명칭금지, 실제halo/글씨/지연비교필요. 실측/코드최종OK아님. 별도이견없어중복A2A ACK안보냄.
- 다음: 기존SH task 구현보고→검증용독립검사→필요시Codex후보검토. DW보수가오면그쪽우선. S5 버전번호·keepalive-multi·A6 APK대응은미결; version인상/게시/폰설치금지. Codex제품변경/빌드/commit 없음.
- **SH 후보c568fbb+2e9428b 검토후NEEDS_CHANGES**: `.claude/codex_sh_filter_impl_r1.md` H1~H5/ledger판정, seq13234회신. H1releaseTimeout→NONE뒤재bind/oldsurface다중소유버그, H2총draw/latch카운터게이트는이미표시된첫frame뒤2번째요구·옛target통과/VIEWER off→weak neverDraw감시없음, H3redraw계수coalesce/세대별분포·전체p95, H4실제APKgate가shseam도통과(rc0ALLPASS5 직접재현)·release testsource차단없음, H5.3EGL출력크기를tap간격으로써resize defer중visible source1px계약위반.
- 남은자동검증: 실APK터치→host좌표·생산shader crop/flip/padding/회전fixture·정확fallback Toast(swap30미확인)·수명/실패/H1H2독립UI. noseamAPK51b35b1d…hash와dex0/shseam1·비교crop직접확인했지만AVD독립UI실행안함. AVD보고p95창중앙값off70.5→weak89.0/strong88.9(+18.5)·입력21fps, 계약4ms미달그대로/폰실기전체미확인. rawJSON은개별샘플아닌5초창요약이라측정법보수필요.
- **수동IP(TCP)10초정지**는별도ledger등재OK(4회/off에서도관측·UdpControlLink10000소스확인,기준판/원인미분리). 하네스11.5초선택지연우회는실사용접속성공근거아님, 신규host개편을SH에섞지않음. 검증용이기록/후속우선순위연결. 기존SH task에묶어보수하며작업용2는DW r2먼저→SH. S5/버전/서명/게시/폰설치미결유지.

## 별건 문서 — NP 직접 연결/중계 분석
- 사용자 ‘문서에적어놔,앞으로구현’에 따라 검증용 문서4후보(main c04eb47 위) 전문검토. `.claude/codex_np_direct_connect_doc_r1.md` **NEEDS_CHANGES N1~N5**, seq13203으로 검증용 pin회신.
- 후보hash 앞12: 신규직접연결문서a12ce02c9ea0·작업목록4e6ecefb80fa·구현계획db8640691294·history8cc93f71fc68. 지적: 표합282vs293/11건차이·eligible분모/재현근거, bound=provisional Hello시도(성공아님)/없음=직접성공아님, port≠43000≠오광고확정, 2500ms=relay응답지연정책(직접실패검출아님), eligible광고주소vswire원본, parserrelay정보손실→UI미재현한계. 후보ⓐ외부STUNⓑUPnPⓒLAN관찰미광고는 설계후보이며 착수/공유기변경승인아님.
- 다음: 검증용보완4고정후보→Codexdelta재검토→검증용Git MCPcommit(push없음). NAS원본293재집계/외부mapping/실제relayUI는Codex미실측. 별건DW158aff3보고도착·검증용독립검사중이라는상태만수신했으며Codex제품검토요청은아직없음. SH는위최신명시재개절을따른다.
- r2 `.claude/codex_np_direct_connect_doc_r2.md` **OK** 회신seq13211. 제공evidence snapshot(3dd218b2…)·aggregate(4f10d790…)·by_port를직접읽고실행exit0,独立id집계293eligible/153bound/151active/140없음·15행/port표일치확인. NAS새수집/원격완전성실증은아님.
- 커밋전본판정으로확정한표기교정: f6e8e54a의이름변경8건포함 회사→집PC전체=123중74bound60.2%(active73/없음49), 기존115/73·63%는옛이름구간한정또는교정; 표2 73/73·24/85불변. .claude자료는repo안미추적(‘밖’아님), 시도시작끝01:28:44와snapshot마지막줄01:28:46구분. 이표기delta+history r2OK만반영후4문서Git MCPcommit 허용/추가검토왕복불요, 최종패킷대기·push없음. 원본evidence커밋/업로드금지.
- NP commit8314d21 직접확인: 승인4문서만·123/74교정·최종hash/diffcheck/trackedclean OK. 다만 **commit본문 non43000=73/73은3478(4/20)누락표현**이어서metadata만‘25870/16016/14570=73/73;43000=24/85’(또는non43000전체77/93)로정정요청(seq13220). 본문정정뒤새commit hash 최종패킷대기, 문서기술재작업불필요/제품진행독립. 기존8314부모c04eb47, push없음.
- 검증용FINAL_PACKET은8314d21로수신했고최종4파일hash가직접확인값(e5b76f0d4aee/3afb5071ab5e/db8640691294/26f5cc0e7d34)과일치한다. origin/main5063cd9·push미실행보고. 수신뒤직접gitlog에서도아직옛본문(non43000표기)이라seq13220정정요청만미결이다. 같은요청을재촉/중복발송하지않았다.
- **NP 최종확정commit83cbd784c75dc787d04299ee979d474a83f41e4d**(8314d21의본문정정, parentc04eb47·tree/4문서hash동일). 정정FINAL_PACKET수신, ‘25870/16016/14570:73/73;43000:24/85’ 정확표현으로종결. NP문서작업완료, 제품NP착수는미승인, push없음(origin/main5063cd9). 이후예전8314hash로본문정정을재요청하지않는다.

## DW 고정 후보 검토 — 158aff3 NEEDS_CHANGES
- 검증용독립검사후요청수신·`.claude/codex_dxgi_wedge_fix_r1.md`로 **D1~D4/E1 보수요청** 회신seq13219. 작업용2기존task t-dreqrszk로검증용이묶어보수지시, SH보다DW보수우선. 새task중복위임금지.
- D1 명시monitor정보가있는데목록미지원/4초미수신이면primary0선택하는우회반려. 논리desktop vs명시monitor구분, 후자identity불확실→picker. DISPLAYname+rect만으로안정outputidentity단정안됨.
- D2 현재reveal/decode때마지막렌더descriptor/Done보고→실제present성공·generation/frame로commit해야함. 이전A실제present→Bdecode만/present실패→재연결A대조필수.
- D3 startup거부future기록을NextRecord가max로재사용해실제wedge영속화도무력화되는반례수선, 최초lockfile share0생성의동시writer sharing위반도bounded직렬화/빈폴더반례.
- D4 test-only 재생성MTarm을MFdevice공개전으로. E1 보수고정후보독립UI사슬/반례검사필요(기존독립검증UI는SKIP). 영향없는GPU전량재반복불요.
- 직접확인: 4개build-vs artifacthash보고일치·store53/0등원본끝줄/gate반례·작업용재연결/복원사진열람. Codex독립제품실행/빌드/실기아님. B4prelogin제외·B5격리우선은해당source/evidenceOK. minidump보류유효, NVIDIA/releasephase/승격ProgramData/재부팅미확인. 방화벽규칙원본시험영향기록유지/임의삭제안함. 구현/통합/출하OK아님.

## LK 고정 후보 검토 — 550a944 NEEDS_CHANGES
- 검증용비UI독립검사후요청을받아 `.claude/codex_lockcred_impl_r1.md` **L1~L6/EVIDENCE**로기존t-rs9nhsdv보수요청(seq13227). 작업용1담당유지·새중복task금지. DW/SH독립계속.
- L1 공용relay결과cache/Poll이requestId만key, viewer새프로세스첫id2재사용→옛terminal로새seal생략/옛성공재사용. cookie+id+purpose/transaction격리·bounded종료정리필수.
- L2 service의spawn전권한검사뒤child Job은session/pw만, expiry/account/lockgen/cancel없고TypeIntoTarget stillValid미연결. 실제clear/type직전까지권한fence/취소회수필수.
- L3 A6는service수신시requester검사뿐, 실제consolefallbacktarget·큐소비agent최종시점lock검사가없음. 실제target과최종inject경계거부필수.
- L4 fieldidentity=HWND+PID라같은창의다른IsPassword요소구분불가·ValuePattern clear뒤fresh동일element검사없음·UIA오류를selectall미지원과혼합·부분Ctrl/A주입keyup정리없음. Q1실반례보수.
- L5 relay동기exchange ReadFile은holdUntil/callerwait밖무한대기, EndSession도queue뒤. 실제I/Odeadline/cancel·서비스cap수명·UI결과generation보수. L6 GNLINK_SIS_TEST_단독표식gate추가(현누출판정아님), DW표식합집합은통합때.
- 직접확인3build-vs hash(Servicee3838e0c…/Stream3db5ad10…/Viewer078b22c3…)·원본41/14/11끝줄/seamscan. Codex빌드/독립제품실행/OS아님. 검증용UI36/prelogin은슬롯미실행, 작업용저장35와최종소스36을PASS합성금지. 보수고정후보독립UI+반례필요, 기존S1간헐2/39미해결/영향분리, 실제Session0→console/LogonUI·Enter·UAC·APK A~DOS실기미확인. 실기A⑧문구는idlecap해제/실transaction직렬한계구분하도록지적. 구현/통합/출하OK아님.

## LK r2 고정 후보 검토 — 8ecaa8f NEEDS_CHANGES
- 최신 요청 r2를 검토해 `.claude/codex_lockcred_impl_r2.md` 작성, 검증용 remote#wwml7h91에 pin 회신 **seq13255**. 기존 t-rs9nhsdv/작업용1에 네 항목 한 묶음 보수 연결, 중복 task 금지. 전달1은 착수/완료 증거가 아니다. 다음 담당 검증용→작업용1 보수→독립 영향 검증→Codex 고정 후보 재검토.
- L2-R: Ready 시 commitDeadline=now+20s만 검사하여 original ctx.expiresMs 무시. 29s sealed 접수→31s Ready(원래30s 만료)도 허용. min(original expiry, processing bound)·무입력 반례 필요.
- L5-R: cv.wait_for predicate가 nonempty 큐이면 true라 hold 마감이 지나도 drop 안 함. busy/status/probe가 계속 queue에 있어도 절대35/130s 마감과 남은 exchange 예산 적용·다음 Host 통과 반례.
- UX-R: idle 무점유/실transaction 직렬 구분 OK, busy 오류0/170만 안내는 반려. 실제 busy 별도 결과·수동 재시도 안내, 일반 InternalError 전체를 busy 해석 금지. stage 추가는 허용(16 등 번호는 검증용 충돌 확인), 비밀 자동 재사용 없음.
- L1-R: EndSession 삭제/취소 뒤 challengeResults_[key], statusAnswered_[key] 무조건 publish가 종료 map 재생성. 살아 있는 transaction 세대 확인·late challenge/status 취소 반례, tombstone 영구 누수 금지.
- 직접확인: verify-lockcred build-vs Service0c59d3d7…/Streama655d4cc…/Viewerdafed71c… 해시 일치. r2 원문52/15/22/21/8/4·policy17/17·gate9/self19·seam0/test6·4. 독립 제품UI lock-screen38/prelogin79/not-prelogin28 원문과 typed/current-generation 캡처2개 확인(입력≠로그인 성공 안내). Codex 테스트 재실행/빌드 없음.
- L3/L4/L6는 소스·격리 근거 수준 보수 확인. A6 sent0/hostRefusals0은 viewer 보류, host refusal4와 구분. 실제 Session0 SYSTEM→콘솔 child·agent 마지막경계·LogonUI·Enter·재부팅·UAC·APK A~D 미검증, S1 미해결. 고아LifecycleTest2개는 작업용 소유자가 고정 신원으로 정리 결과 연결. 구현/통합/출하 OK 아님·PC버전 인상/게시/push 없음.

## DW r2 고정 후보 검토 — f0ee888 SOURCE OK / EVIDENCE 보완
- `.claude/codex_dxgi_wedge_fix_r2.md` 작성·검증용 remote#wwml7h91 pin 회신 **seq13259**. 기존 **t-dreqrszk**에 E2/E3 근거 보완 연결. delivered1은 연결/착수 증거 아님. 제품 defect 추가 발견 판정이 아니라 실제 프로세스 반례 증거 잔여다. 다음 담당 검증용→필요시 작업용2 기존task→독립 실행→Codex 재검토.
- D1 SOURCE OK: logical desktop만 자동복원, explicitmonitor/window는 picker. 목록누락4초우회/이름rect매칭 삭제. toolbar legacy monitor요청 접수시 monitor로 보수적 downgrade, 새 wire 안정identity는 별도설계로보류. E2는 실제toolbar/명시monitor→보고→shell→새viewer picker+사유·자동desktop0·수동선택 근거 필요(두번째물리monitor 안정identity 증명요구아님).
- D2 SOURCE OK: reveal후generation후보 Arm, 실제paint presented&&!pickerVisible에서commit/report. helper순수시험/commit_on_arm mutant 통과지만 E3 실제프로세스 A present→B ACK/decode→B present실패→B보고0·shellA유지→재연결A 및 B성공대조 미실행. 격리testbuild renderer실패주입허용·출하seam부재/양성대조필수. 전체desktop사슬반복불필요.
- D3 SOURCE/EVIDENCE OK: 미래기록수선·최초lock shareRW/retry 동일3s/보호검사유지, 독립64×3/첫lock20round bad0·미래/lock mutant실패. D4 test-only 새ctx직후MT→WinRT/MF 순서 SOURCE OK, recreate실행은미관측·MT출하보류.
- 직접 verify-eop HEAD f0ee888/clean·Stream4ce6197a… Host14a06bee… Viewer6fbc465b… Clientde595ba0… MtAba8b7057f…/shell8fac49cd…hash 일치, 원본 store64×3/restore30/quarantine23/snapshot7/gate9/self18·4mutant 실패확인. 독립UI84/사슬38·19.8s·reveal→101ms/64ms뒤presentreport→restored 원문과 reconnect/restored PNG직접열람. r1 E1 desktop독립사슬은닫음. Codex빌드/제품실행아님.
- 승격ProgramData실쓰기/재부팅/NVIDIA/release-phase wedge 미검증. 사슬folder-unresolved err3라영속기록증명아님. stagingWARN/pid0표시비차단. E2/E3전완료·통합/출하승인아님. 버전/서명/게시/설치/push없음, SH task별도.

## SH r2 고정 후보 검토 — 7609d0b NEEDS_CHANGES
- `.claude/codex_sh_filter_impl_r2.md` 작성·검증용 remote#wwml7h91 pin **seq13292**. 기존 t-b7ss11ov/작업용2에 R1/R2/R3 한 묶음 보수 연결. 다음 담당 검증용→기존작업용2→고정후보독립실행→Codex재검토. delivered1은착수/완료아님.
- R1 직접재현: seam=true Gradle ':app:assembleRel' --dry-run **rc0 BUILD SUCCESSFUL**, 실제graph packageRelease/assembleRelease 포함. ':app:assembleRelease'는rc1 refusing정상대조. startParameter문자열검사만해서축약우회. resolvedvariant/task차단또는release항상noseam구조필요. dry-run만실행, 실제compile/package없음. JDK C:/Program Files/Android/Android Studio/jbr, SDK C:/Users/shota/AppData/Local/Android/Sdk 프로세스환경만설정. PS속성인자는반드시 '-Pgnlink.shTestSeam=true' 따옴표(미인용은잘못분리되어무효).
- R2 소스경합: android_video_decoder.cpp releaseOutputBuffer(AtTime) 뒤 handoff ring공개. native preempt사이 GL/UI첫latch가info=null이면timestamp는LatchClassifier에기록되어나중동일latch는redraw earlyreturn, 정지원본은두번째frame없이3s불필요fallback. metadata-before-buffer또는bounded재평가·공개지연반례필요. 현재AVD발생실측주장아님. H5.3 src크기도이경합에영향.
- R3 자동근거잔여: 검증용핵심AVD독립실행(H1hold/H2never/첫출력/resize), 실제생산shader crop/flip/padding fixture(1088 decoder한계와별개), native큐→격리Host좌표수신. 작업용touch16은앱큐까지만. 실사용자desktop주입불필요. 최종identity로그mean.179/p99=3/max223 전체picture인데보고.10/max10·움직이는띠제외와달라기록정정필요.
- 직접 APK final2-noseam a77520a0… / shseam b9a1deb8… hash일치. gate직접noseamrc0 ALLPASS6 /shseamrc1 표식3검출, 이부분H4닫음. worker final2compare25/lifecycle14/resize8 + final3latency2/identity3/faults28(정확Toast4)/viewer_never7/hold11/touch16 원문확인. off/weak PNG직접열람. 검증용/코덱스 AVD독립실행은아직없음. H1 BLOCKED·개별surface/늦은release, H2이벤트gate, H3 nearest-rank구간통계·H5.3 frame size/회전cache보완확인, R2잔여.
- S4 whole-segment off73.3/75.0, weak92.0/strong91.7ms(+16.7~18.7) **AVD미달유지**, latency2PASS는수집검사이지성능PASS아님. 입력21fps로30/45/60미증명. 폰모두미검증·S5미결·0.2.30/29불변·버전/서명/게시/설치/push미허용. Codex변경은판정서/인계뿐, 제품trackedclean.

## LK r3 검토 — 53506ab NEEDS_CHANGES, 잔여 R1/R2
- `.claude/codex_lockcred_impl_r3.md` 작성·검증용 remote#wwml7h91 pin **seq13294**. 기존t-rs9nhsdv/작업용1에 두 항목만 보수 연결, delivered1은착수/완료아님. 다음담당검증용→보수→독립영향검증→Codex고정후보재검토.
- L2-R 원래expiry min보수/경계반례, L1-R liveTx/txGen 늦은cache 차단은닫음. Busy16·실제pipebusy·무입력·A무간섭·A끝난후수동retry입력교체/해제의독립UI근거수용. 일반오류PostMessage는문구대조, relay실패분류는단위근거로구분. sealedBusy단위+공통문구코드도한계표시하여수용, 이것만으로UI전량추가요구없음.
- **R1** 동일typed transaction의sealed재전송: SealedAsync는terminal만중복생략, service는재입력없이Injected반환, relay540~546이매번hold_for(130s)로마감새로생성. t0typed→t119duplicate→마감249초, 그뒤poll없으면service ReadFile대기(120s상태판정자동pipe반환아님). 최초typed마감보존필수, hold2s단축시험+동일sealed여러회/지연/terminal대조·마감갱신변이실패/다음Host통과. 실249초재현실행주장아님.
- **R2** Busy '길어야2분'은typed130초·challenge→typed전환상총대기보장못함. 숫자만삭제하는최소안 '그 요청이 끝나거나 취소된 뒤 다시 누르세요.' 허용, 보고/현장묶음맞춤. 같은후보BusyUI1회판독캡처+relay독립영향/gate/hash, 무관전체회귀불필요.
- 직접3artifacthash Service dcd98c97… Stream a254a976… Viewer faae692b… 일치. 독립원문relay37×3/lockcred54/service12/channel15/target21/refusal4/policy17·17/preloginhost43/gate9self19/seam0test6·4, UI44×2/79/28 확인. Busy/일반오류 PNG직접열람. 고아2개는검증시없음해소. 실제Session0SYSTEM/LogonUI/재부팅/UAC/APK/두PCBusy·S1미검증유지, 제품버전·게시·push없음. 코덱스제품수정/빌드/실기없음.

## DW r3 검토 — 4db9f17 NEEDS_CHANGES, E3닫음/출하목록보수
- `.claude/codex_dxgi_wedge_fix_r3.md` 작성·검증용 remote#wwml7h91 pin **seq13298**. ①mutant/③monitor조회는기존 t-dreqrszk/작업용2 한묶음보수, ②hello는별도결함후보 제한조사. 전달1은착수/완료아님. 다음검증용→기존작업용2→독립고정후보→Codex.
- **①** monitor_as_desktop FAIL은null failReason→std::string충돌0xC0000005라무효. guard+decision함께우회해실제잘못된desktop선택/present에서FAIL하는유효변이필요. crash/timeout을안전성검출로세지않고worker보고정정.
- **E3닫음:** 4회모두B보고0/shellA보존, 성공3회A즉시복원·실패1회도두번째자동attemptA복원, presentok36/report_on_reveal33/3유효차이확인. 기존35/1FAIL을소급PASS로바꾸지않음. 첫새PID만성공해야하는새요구없음; 기존총관찰기한/최대3회정책안operation/attempt전체A복귀로runner정정가능, 첫실패/지연별도기록필수.
- **②hello** 1/4 firstattempt실패·hello11.252s/181attempt25badAck·hostepoch2종료뒤hello ack·목록timeout은복원descriptor와별개재연결결함후보. 기준판/후보동일Host생존+Viewer재생성fixture/capability/peer/epoch/control타임라인제한조사. r3diff밖만으로baseline원인종결금지. 인증완화/timeout증가/retry확대승인아님. ledger담당/다음연결명시.
- **③포함승인:** 출하Viewer에RequestMonitorList0/toolbar버튼없음동적확인. main5063cd9도소스상같지만기존결함이라허용하지않음. 현재지원cap획득후picker독립초기목록조회(기존큐·중복제한), 미지원요청0, 조회가자동select/stream/gen/descriptor변경하면안됨. 시험Viewer REQUEST_MONITORS보조없이출하Viewer로toolbar→E2사슬·유효mutant독립확인, 가상2번째monitor허용/실물identity성공아님. 무관store/GPU/E3전량반복불필요.
- 직접Viewer2005ca23…/R3Testc56f5a72…hash·gate9self20/출하seam0기록, desktop38/E2 42/E3최초35/1+37×3/presentok36/변이실패원문확인·E2picker부제/E3A PNG열람. 제품동등presentfail분기수용. D1~D4소스판정유지, 통합/출하OK아님. 회사PC/NVIDIA/release-phase/승격ProgramData/재부팅/D4재생성/실2번째output미검증, 버전·게시·push없음.

## LK r4 검토 — e728485 SOURCE/EVIDENCE OK, 자동보수닫음
- `.claude/codex_lockcred_impl_r4.md` 작성·검증용 remote#wwml7h91 pin **seq13315**. 기존t-rs9nhsdv의R1/R2모두닫음·추가보수없음. 상태 **LK테스트후보·OS실기대기**, 실제OS성공/전체구현완료/일반릴리스·게시승인아님. 검증용이e728485고정후통합검토입력·현장묶음유지, DW/SH기존task계속. 같은근거의재검사/승인왕복금지.
- 후보 **e728485047a7c23402c45578e64b621e552c6bcb**, 제품+시험661a0c3/문서e728485. 제품diff53506ab후relay동일cookie/id/txGen이미typed면hold_for생략(최초typed만130s), Busy문구숫자삭제 두파일뿐. 동일기능제품/시험한커밋의기록공개수용·재작성불요.
- 직접verifyHEAD/clean/diffcheck·Streame0952a8f…/Viewerb4ea8363…/Servicedcd98c97…hash일치(서비스r3동일). 원본relay41×3·동일sealed재전송3회·다음Host2347/2350/2361ms·Unconfirmed15·terminal뒤0 확인. hold2s와중복후가상3.6s마감차이를구별, 실제130/249s실행주장아님. 변이실패worker인용으로구분.
- 독립UI44·실제HostA시험pipepending→Busy16·무입력/A무간섭→종료뒤수동retrytyped8/바이트교체/unlocked9·새BusyPNG직접열람. gate9self19/seam출하0시험6·4 원문확인. CodeX제품build/실행아님.
- 이전L1/L2/정책/교체/cache보수판정유지. 실제Session0SYSTEM→console/agent마지막경계/LogonUI·Enter·재부팅·UAC·APK A~D·두실HostBusy·sealedBusy UI미검증/S1미해결유지. 통합시공통protocol/gate합집합·영향검증·새buildcommit/hash고정, 단독hash재사용금지. APK영향/S5미결·버전/서명/게시/설치/push범위불변. 사용자현재조치없음.

## reconnect-hello-delay r1 설계 판정 — 수정착수/0.2.162후보포함
- 조사보고 `.claude/report_reconnect_hello_delay_r1.md`/task **t-uu1zshde** 전문검토, `.claude/codex_reconnect_hello_delay_r1.md` 작성·검증용 remote#wwml7h91 pin **seq13316**. 원인기제는수정착수근거충분/효과아직미검증. **①PunchAny 송신cadence150ms보장+②Hello동일round내bounded수신 함께채택**, 별도기능commit으로PC0.2.162통합후보포함방향승인.
- 기준판5063cd9도실재현(base14중4>3s/cand14중1/진단사슬20중2,표본/진단효과로빈도비교금지). 관련제품rendezvous/hello/host등diff0 직접확인, base mgxmQQ8057/6411ms·diag eybk6A6102ms/연속10054 wait0→Punch303→Ack301원문확인. hostack송신성공은그시각수신큐도착wire증거아님·recvfrom실패from귀속단정금지·Punch25개출처추정유지. 실LAN/NAT/회사PC전체원인확정아님.
- ①monotonicnextSend, 오류/zero/junk로재송신round당기지않음, 응답후즉시선택·cancel/budget/CPUbounded·catchupburst금지. ②ACK아닌항목마다sleep/새Hello금지, 같은slice내읽고정상재시도시계에서만재송신. 단순sleep삭제로송신폭주금지, finitebatch/deadline/stop·fatal오류구분. 기존ACK/header/features/Auth/connectedpeer·hostpunch25/200/50·timeout/3회정책불변.
- **사전큐전체flush/SIO_UDP_CONNRESET변경은최소안제외.** Microsoft공식recvfrom/IOCTL문서확인·판정서링크. ICMP옵션으로증상가리지않고실오류에서루프정확성검증. 수동IP10sledger와원인같다고합치지않음.
- 필수시험: 즉시오류폭주송신상한+유효다른후보/ACK앞25Punch+100여오류항목처리통제1s내·구식sleep변이/연속junk무응답budget/cancel/auth음성/실11s유지재연결최소10+4s대조·실제DW사슬/Android공유옵션0·50영향native빌드/고정후보검증용독립·seam출하부재양성. 무관GPU/store전량불필요.
- **다음담당검증용**: 조사t-uu1zshde근거닫고작업용1최초구현task1개생성·ID/담당연결, `.claude/task_reconnect_hello_delay_impl_r1.md`같은통합정본1개. 조사/구현ID구분·작업용2중복지시금지. 구현→자체→검증용독립→Codex고정후보, LK/DW/SH별도commit통합. delivered1은task생성/착수증거아님. 아직구현task ID미수신.
- 승인범위=최소루프수정/시험·관련기록commit준비/0.2.162통합후보방향, 버전인상/서명/게시/사용자설치/push·releaseOK아님. APK S5미결유지·실서비스/폰/회사PC무조작. 사용자현재할일없음. Codex제품수정/빌드/실행없음.

## DW r4 검토 — fc4cd57 SOURCE/EVIDENCE OK, 자동보수닫음
- `.claude/codex_dxgi_wedge_fix_r4.md` 작성·검증용 remote#wwml7h91 pin **seq13321**. 기존 t-dreqrszk 잔여①/③닫음·현재DW추가보수없음, 이전D1~D4/E3판정유지. 상태 **DW테스트·통합검토입력후보/회사PC미검증**, 전체연결문제해결·제품완료·일반릴리스/게시승인아님.
- 후보 **fc4cd57dfba81a70d37b0361ab6200baab84337f**. 출하Viewer flags최초false→true시조회·Run초기capreset·반복WindowList재요청없음·미지원이중차단·ApplyMonitorList는목록만/restore descriptor변경없음·시험Viewer조회보조제거. 기존--monitorN 분기보존/같은Runresume재초기화없음은소스근거한계수용, 추가전량요구없음.
- 독립출하Viewer monitor44/nomonitors29/desktop38 원문·pid별목록1회→실toolbar선택1회→monitor보고→exit44→restoremonitor→picker사유/자동0→수동desktop present확인, 툴바 '모니터1▾' PNG직접열람. 유효mutant38/1 WRONG에서실desktop select applied/충돌0. 첫mutant시도는hello12.835s로단계전INVALID유지, 소급성공금지.
- 직접verifyHEAD/clean/diffcheck·Viewer57c4e0c33eaee1fd0d2e0efc2295aab241bfcbaec6d6af667330108981c15fbe/Stream673e5f8caeda16d5bc348365c33d3bae27eacdf108dc41d166e45a0b539b7ac8 hash일치·gate9self20원문/seam0양성자료확인. Stream소스변경은test#ifdef/중괄호로출하동작변경없음수용·바이트동일성주장/추가전처리재현빌드요구없음.
- 검증용이DW fc4cd57/LK e728485/별도hello/SH미결각고정commit으로통합관리·공통gate/protocol영향·새build/hash고정후Codex통합검토. 첫연결에서도hello지연3회/9connection·12.835s실패관찰을별도hello과제에연결. hello수정검증전전체연결해결주장금지.
- 회사PC NVIDIA/release-phase/승격ProgramData실쓰기/재부팅/D4재생성/실물2번째outputidentity미검증. desktop만자동복원·monitor/windowpicker 제한유지. MT/dump/Bⓑ2보류·버전/서명/게시/설치/push범위불변·사용자현재조치없음. Codex제품수정/빌드/실행없음.

## hello-delay-fix 구현 연결 확인
- 검증용delegation-linked수신·`.claude/task_hello_delay_fix_r1.md` 전문확인. **최초구현 task t-q4eq9aiz / 작업용1 remote#rqxbj3jb / fix/hello-delay(base5063cd9)**. 조사 t-uu1zshde와구분, 설계①+②및시험정본구속참조일치. 단순ACK회신없음·착수/완료주장없음. 다음작업용1구현보고 `.claude/report_hello_delay_fix_r1.md`→검증용독립→Codex고정후보검토.
- 정본의 '조사용scratch inv-hello-* 정리대상'은C:/work/gnlink-wt로repo밖삭제라사용자AGENTS최상위정책과충돌. 검증용에scope-correction1회전달(**seq13323**): **이번task에서밖삭제하지말고보존/정본정정**, junction rmdir지침도삭제허가아님. cleanup은필수아니므로사용자질문/제품진행중단없음, 새task/ACK불필요. 구현승인범위변경없음.
- PC0.2.162통합후보방향·버전/서명/게시/설치/push미허용·실서비스/세션무조작 유지. 조사scratch는증거자료로보존, Codex파일삭제없음.

## SH r3 검토 — c494dc6 SOURCE OK / touch_host 독립1회잔여
- `.claude/codex_sh_filter_impl_r3.md` 작성·검증용 remote#wwml7h91 pin **seq13332**. 후보 **c494dc656b9aff372fd4d03997c8a1e44e514eea**. R1release guard/R2publication SOURCE닫음·새제품보수없음. **EVIDENCE NEEDS_CHANGES: 검증용 touch_host 독립1회만잔여**, 기존 t-b7ss11ov에서검증용직접→결과/기록회신. 작업용2재보수/전량재실행불필요.
- 독립새worktree verify-sh APK직접빌드확인: noseam0d44725b882837ec30bebed168a48d6b433385be8cbc5322491ffd843b9be74a /shseamf8218612a6990a6d45caa45fe19bea8b1ef84fa265dde92ebca6a0adcd28aca9, fixtureHost0771e768…f152f56 workerDW빌드복사·새Host독립빌드아님으로구분. 직접hash일치/head/clean/diffcheck·원문compare25/hold11/never7/resize8/race5/shader25/latency2수집/gate7음성2FAIL확인·글씨crop열람. Gradle assembleRel/aR등resolvedgraph거부·noseam/debug대조는검증용귀속.
- 생산ringmetadata→key→buffer release·실패key회수 source확인. 고친순서latchesBefore0/구식lateentryfallback 독립반례, H1blockedproducer0/늦은해제1회·H2viewer never 원본복귀·shaderactualBufferQueue/OES/ImageReader crop/mirror/rot90/no-ring·unclamped37/noswap34.85 대조확보. 모든crop조합/실Codec2아님한계유지.
- worker touch_host18/0×2 원문/실owned창 background_message경로읽음, verifier미실행이라off/.20/.35/크게보기native큐→창수신1px 독립1회요청. realSendInput/폰필요없음. **하네스pattern_input.txt삭제있어새--out은repo안D:/remote/remote/.claude/test-tmp/verify-sh-touch-r3/**로지정, C:/work자료삭제금지(AGENTSroot정책).
- worker이전크게보기세션종료는좌표미판정+제품control실패관찰. 환경INVALID로실패지우기/소급PASS금지, clipboardpoll실패1회별도ledger·수동IP10s/hello와원인같다단정금지. 현재SH Host/control개편승인없음.
- S4독립AVDp95 off74.7/72.8 vsweak87.9/strong87.8 **+13.1~15.1ms기준미달**. '어느쪽도S4판정근거아님'은실폰에한정하도록정정요청, AVD실패지우지않음. r2identitymean.179/p99=3/max223전체영역정정수용. 폰모두/실SendInput/release서명미검증, S5미결·APK0.2.30/29·설치/게시/push보류유지. 사용자현재조치없음. Codex빌드/제품실행없음.

## 운영 사고 — root 밖 inv-hello 3개 삭제(보고·읽기 확인)
- 검증용이hello-delay-fix/t-q4eq9aiz fact incident전달: 정정도착전에작업용1이잘못된정본문구 '정리대상'에따라 **C:/work/gnlink-wt/inv-hello-base,cand,diag 3개를삭제**(junction rmdir→reparse0→git worktree remove --force, 명령절차는검증용보고귀속). 사용자AGENTS최상위root밖삭제금지위반이며정본문구책임은검증용이인정·사용자안내했다고보고. 허용된정리로재해석하지않음.
- Codex읽기직접확인: 위3경로Test-Path False·git worktree목록부재. **C:/work/gnlink-wt/lockcred-scratch/hello_evidence/**에대표base mgxmQQ/diag eybk6A·b55Nxq·wyahk0등9폴더존재. D:/remote/remote/third_party/webview2/build/native/include/WebView2.h존재True. 원SDK전체hash/삭제전모든미커밋진단파일의완전성까지감사한것은아님.
- 현재inv-hello-before및fix-hello-delay worktree존재. **이후inv-hello-*및기존증거자료보존**, 추가root밖삭제불허정정유효(기존Codex scope-correction seq13323). Codex삭제/복구/커밋없음·사고ACK재송신없음. 필요한기능작업t-q4eq9aiz의구현/검증범위는유지하고새수정/정리승인으로확대하지않음.

## SH r3 touch_host 추가 — 코드/기능별 EVIDENCE OK, control/S4/폰잔여
- 검증용추가evidence수신후 `.claude/codex_sh_filter_impl_r3.md`를**최종상태로갱신**: c494dc6 SOURCE/기능별EVIDENCE OK, 남았던touch독립기하검증닫음·현재SH추가보수/반복요청없음. 검증용remote#wwml7h91에pin evidence-review-result회신 **seq13340**. 상태=SH코드·출력/기하검토입력후보, 전체기능완료/폰실기만대기/통합·게시승인아님.
- repo안 `.claude/test-tmp/verify-sh-touch-r3/` 첫회rc1 FAILED6/5: GUARD통과뒤첫탭전window-selectapplied10:28:00.632→TCPdisconnect.633/앱control loop failed. **좌표미판정+제품control실패관찰**로유지·환경INVALID/소급PASS금지. 약10sUDPsessionend도관찰.
- attempt2rc0 ALLPASS18/0: sourceguard/기록→앱native큐→Hostbackgroundmessage→owned창수신8줄직접대조. off/.20/.35(670,569), 크게보기(669,569) 모두Δ0. 확대PNG직접열람(화면이동/전체보기), 실SendInput/실폰아님. 기존독립APK/Hosthash고정유지·실행전재확인은검증용귀속.
- 둘째성공으로첫control실패닫지않음. workerclipboard sync failed/이번control loop failed/수동IP10s/hello원인동일단정금지. 성공·실패둘다UDP10s종료가있음→그것만으로TCP원인확정불가, 성공회차PCclipboard변경관찰도인과근거아님. **검증용이구분ledger/다음담당유지·보존로그제한원인분리**, SH Host/control개편추가승인없음. 최종통합연결신뢰성영향필수보고.
- S4 AVD+13.1~15.1ms미달유효/폰S4전용불가·실폰전부/release서명/SendInput미검증, S5미결·버전0.2.30/29·설치/게시/push불변. c494dc6고정·기능별최종기록취합후LK/DW/hello 통합새commit/hash/gate영향검증. 사용자현재조치없음·Codex제품수정/빌드/실행없음.

## hello-delay-fix r1 구현검토 — 83c3ac5 NEEDS_CHANGES
- `.claude/codex_hello_delay_fix_r1.md` 작성·검증용remote#wwml7h91 pin **seq13359**. 후보 **83c3ac5b8b3fd2e42e2da9d46cbe985de79b506b**(제품7646eef/시험4d8e4a8/문서83c3ac5). 기존t-q4eq9aiz/작업용1에 **N1/N2한묶음보수**, 새task불필요. 다음검증용→작업용1→고정후보독립→Codex.
- **N1** hello reads512상한을slice200/250보다빨리채우면roundEnd안기다리고retrySleep50/0뒤다음Hello. 계약clock우회·Android즉시재송신가능. 현재Flood Sleep1로slice당512미도달. cap경계실도달·정상송신clock보존/ACK·budget/cancel/CPU대조·원복변이필요, 단순cap확대/timeout증가불가. oldhpp주석pause-after-badack는같은편집서수정.
- **N2** PunchAny새범용Terminal전부fatal은UDP개별advisory까지끝냄. **WSAENETRESET=UDP TTL만료**MS공식recvfrom문서확인(판정서링크), 한후보오류→다른유효후보놓침/첫후보fallback. PunchAny로컬분류만보수·TTL뒤다른후보승리/진짜NOTSOCK실패·연속오류cadence대조, 전체connectedrecv광역정책변경금지. POSIX실오류미검증유지.
- directory_retry후보1/4FAIL132/133(나머지3PASS)·base3/3PASS는기존/무관증명아님. **ledger미해결유지**. RunHost99조건에서15s+300ms후비동기Status1회샘플, health/observe반복후timeout대observations라상태시점/cooldown/HTTPdefaultobserve의미제한조사필요. 원인단정·환경INVALID·성공할때까지전량반복·기대문구완화PASS금지. 제품진단변경은별도근거.
- 직접artifactViewer82540434c33be03f2850d08e2690b17981e41d52b34f020315441ce719408bc4/Streamb7b4d242…/Clientf043508f…hash일치, 원문backlog18×2/punch10round1500ms/late626/stop309·gate9self16·directoryFAIL확인. verify-hello는clean출하hash뒤Hostseam적용됐으므로현재clean주장금지. productloop파일diff없음확인.
- 실재현개선근거수용: base8.661s/139attempt/bad25 vs후보11s유지재연결10회262~539ms>3s0(검증용귀속), chain原文첫534/재연결262ms/attempt2/recvErrors0→desktop restored gen4직접확인. H1-H3변이/Android4ABI빌드는worker인용·독립아님. 이정상개선전체를부정하지않음.
- N1/N2영향반례/인증/취소·Android옵션0/250·새nativebuild/gate/hash·소수실재현/사슬독립에집중, 전량29suite/10회무조건재반복불필요. PC0.2.162포함방향유지·릴리스/버전/서명/설치/게시/push미승인, SH control/clipboard원인같다단정금지. inv/verify자료/junction보존·root밖삭제금지. Codex제품수정/빌드/실행없음.

## hello-delay-fix r2 — 6e89962 SOURCE/EVIDENCE OK, N1/N2닫음
- `.claude/codex_hello_delay_fix_r2.md` 작성·검증용remote#wwml7h91 pin review-result회신 **seq13370**. 후보 **6e89962d7c21adc9f74756e829584bff821f41d9**(제품bcb6a46/시험3e625d8/문서6e89962). 기존t-q4eq9aiz N1/N2 SOURCE/EVIDENCE OK·추가제품보수/같은시험반복불필요. 상태통합검토입력후보, 전체회귀PASS/릴리스OK아님.
- N1fullbatch512→≤10ms휴식→같은roundEnd계속, 다음Hello시계보존·ACK/auth/budget불변. 독립28×2/torrent52·65fullbatch→Hello각4/2000항목ACK50ms1라운드/stop310·mutant_batchPC17·Android305·ACK4round로3FAIL 확인. N2PunchAny로컬r1계속집합+WSAENETRESET, 전역recv분류diff0. TTL13회뒤203~205ms다른후보채택/NOTSOCK10038·10round·stop·mutant10052포기로6FAIL. 실제TTL은seam·POSIX/Android실기미검증.
- 직접hash Viewer9088e8fc27d302162d0cd64b44e409615669c24d86d98aae82a404662fda714f/Streamd69988fa…/Client66f80c4a…일치. 독립원문/gate9self16·소스/diffcheck확인. 실제사슬첫533/재연결269ms→desktoprestored gen4직접대조, 11s유지재연결8회>3초0은검증용기록귀속. Codex빌드/제품시험재실행아님.
- 판단① **viewer_udp_recovery같은3FAIL base/candidate ledger미해결**, qemu0.15core/s/전체CPU3~4%는worker 'qemu부하원인'뒷받침안함. **보고+historyqemu귀속정정필수**, 원인미확정·담당연결. directory_retrycachedsilent간헐과원인합치지않음. threshold완화/성공까지전량반복금지. 문구정정은관련기능/통합기록commit에취합·제품불변메타회신으로충분, 재제품검토왕복불요.
- 판단② gate9가새3매크로검사한것아님. **현재빌드는CMake/생성vcxproj부재+시험target양성·실변이대조로분리수용**. root직접전체vcxproj검색결과시험3프로젝트만양성, 출하Viewer없음·Stream/Client해당cpp미컴파일은검증용대조수용. 통합빌드에서도각출하target금지define부재/시험양성/artifact선택확인필수(자동화가능). 전처리macro를exe문자열grep만해서부재증명했다고주장금지.
- 판단③ **제품소스 test-only #if유지허용**(시험target만정의·출하runtime/env토글금지). 루프복제/별도리팩터링필요없음. Gate9+수동compile대조귀속정확히분리.
- 다음검증용이hello6e89962/LKe728485/DWfc4cd57/SHc494dc6각고정통합·새build/hash/공통gate/compiledefine·영향UI/재접속독립→Codex최종통합. 회귀ledger/SHcontrol/S4AVD미달/실기한계유지. 버전/서명/게시/사용자설치/push허가아님·scratch/junction/증거보존·root밖삭제금지. 사용자현재조치없음.

## integ-0162 r2 검토 — 9f54912 통합 OK / 로컬rel후보준비승인
- `.claude/codex_integ_0162_r1.md` 작성·검증용remote#wwml7h91 pin **seq13389**. 후보 **9f5491216c8eb8ef87d5f1d702e49193f15dc68b** = main83cbd78+LK e728485(29c9c23)+DWfc4cd57(6083fb5)+교차시험71d6834+hello d0fe74e(9f54912; d0fe는6e89962와제품같음/qemu귀속문서정정).
- **통합구성/격리 SOURCE/EVIDENCE OK, rel/0.2.162 로컬실기후보준비승인**. 아직실기/전체제품완료·일반릴리스/게시/설치승인아님. 현재PC통합에SH/새APK없음. 다음검증용이rel정본1개/실제담당taskID연결(기존task있으면재사용)→작업용준비→독립최종version/hash확인→CodexFINAL_PACKET. 아직reltaskID미수신.
- Codex직접Gitblob감사 **apps94파일정확합집합/89개원브랜치또는r1bblob동일/겹친5개LK→DW→hello3-way재현**. viewer_control_client Run승인충돌1곳(세대대입fetch_add1회+DWcapreset)해소하면5개모두바이트동일. 재현자료는repo안 `.claude/test-tmp/codex-integ-merge-9f54912/`만. productfile변경없음. 원mergecommit은GitCLI두parent보존보고, 다음versioncommit은GitMCP지정.
- 독립9exe SHA256모두직접일치(Verify: Streamb5e8a644… Hoste2232198… Viewer470d275d… Client0d88e37b… Service00ecf92e… Updaterf35c5f43…/Setupd3cfa20e…). shell소스/payload/Release3곳8fac49cd…직접일치. `.verify/defines_check.py`검토후읽기전용직접실행: 출하9/전이ProjectReference+/D시험0/양성7 ALLPASS. gate9self23/30marker출하0·시험5양성기록확인.
- 독립논리/변이·UI44/79/28/recovery84·desktop38/presentfail37/monitor44/nomonitors29·**교차lockscreen53/0**원문확인. 첫/재연결id7·id3공존/목록각1회/문자보류/LockCredV1재광고, 서비스없음negativeLK4항목만FAIL확인. BusyPNG/교차restoredBMP직접열람(두버튼실제렌더). hello9회≤547msloopback. 제품UI/시험셸·Host/Stream·비SYSTEM서비스/가짜lock층구분.
- **보고정정필수**: presentfail hello표Viewer=GNLinkViewerR3Test(출하exe아님), LK e2e도in-process제품코드. metadata정정만제품재검토왕복불요. pipe대기10→30s도재발한초기실패는제품항목전준비INVALID·원인미확정, fs.existsSync가설단정금지. 현재rel준비비차단/시험준비관측후속담당연결·대기계속확대금지.
- 승인다음범위: **9f54912→rel/0.2.162 PC버전commit(현재0.2.161, product_version.hpp UTF8BOM efbbbf직접확인/줄바꿈보존)**+관련history/계획checkbox. 새build/testinstaller/hash·4exe(Viewer/Stream/Host/Client)0.2.162/old잔존0·gate/compiledefine/HTML/영향smoke독립. InputService/Updater는원래literal없음/억지추가변경불필요. 이전integ산출물hash를새releasehash로쓰지않음.
- 후보검증뒤 **로컬서명/검증·gnlink_deploy --dry-run허용**, PC보호키만사용/비밀외부금지. **실NAS업로드/manifest교체/일반자동업데이트게시/gitpush/사용자HostClientAPK설치·재시작·잠금·로그오프·재부팅·SCM교체는이번승인아님**(OS핵심미검증실기후보). 시험설치본/위치/commit/hash/서명자/기존설치영향/rollback/최소사용자A~D+회사PC묶음완성후단일FINAL_PACKET. 준비전추상설치허가질문불필요.
- 안내필수: 새Host는APK0.2.30잠금화면평문암호차단, SH/새APKsealed미포함/S5미결, logicaldesktop만자동복원·explicitmonitor/windowpicker재선택. 비밀번호파일/argv/log금지.
- OSA~D·회사PCNVIDIA/실wedge/ProgramData영속/실2출력·Android/LAN/NAT미검증, directory_retryt-azc166pv/viewerrecovery처리량/SHcontrol/S4AVD미달ledger유지. 미승인실험rel끼워넣기금지·MT/dump/Bⓑ2보류·root밖삭제금지. 사용자지금할일=로컬실기후보준비보고전없음. Codex제품편집/빌드/시험/서명/게시없음.

## directory-retry-flake r1 조사판정 — 기존기제확인/0.2.162분리후속A+B
- `.claude/report_directory_retry_flake_r1.md`/조사 **t-azc166pv** 검토, `.claude/codex_directory_retry_flake_r1.md` 작성·검증용remote#wwml7h91 pin회신 **seq13397**. **조사근거OK**, cached-silent는기존상태전환+fixed15s단일샘플경계·hello논리미도달. base01sleep202msFAIL/02sleep0PASS 원문/source직접확인. 전체10+10분포는보고귀속, 5/10vs6/10을동일분포/실망50%증명으로확대금지.
- **0.2.162와분리ledger/후속관리**, 현재9f54912통합OK/rel준비정본seq13389이미전달참조(대기로멈출필요없음). 조사상태=기제확인·미수정/원본FAIL유지. 후속A/B함께채택조건정본참조, 현재rel/제품재검토열지않음.
- A그대로단순서버이유유지는불가: HTTP기본문서규칙port+1관찰시도유지·'광고없음+기본주소실timeout'두사실진단, 서버오설정단정/실timeout삭제금지. HTTPS주소없음/명시pin·광고주소timeout/legacy실성공→online대조, health실패vs광고없음구분·출처/이유명시값(문자열검색/bool만추측금지), ready/network/auth/cooldown변경없음.
- B서버healthCount2직후~1.5s창샘플도경합불가: 클라이언트동일cycle상태완료·필요시test-only barrier/clock관측, 불가능한99목표+고정15s단일샘플제거. inactive목표heartbeat3/기대≤2계약은구간·응답단계로해소·숫자/문구완화금지. 핵심분기/회복·부정변이·bounded반복/독립검사, 전량10회다시반복불요.
- 범위주의 **'a silent host loses to relay'(:638)는실제PunchAny호출**. cached-silent결론을시험파일전체hello미도달로확장금지, relay1/10baselineFAIL 별도미확정ledger. inactive도별도case. viewerrecovery/SHcontrol/clipboard/hello와원인합치지않음.
- 다음검증용이조사닫고최초후속구현task1개/정본·담당연결. **0.2.162로컬후보준비우선배정후여유담당별도branch병행가능**, 실기사용자응답필수대기아님. 차기유지보수후보/새버전·이번relbackport확정없음. 현재후속taskID미수신. 제품수정/빌드/실행·commit·push없음, root밖자료보존·사용자현재조치없음.

## rel-0162 게시 지시 수신 — NAS진행 / push근거원문확인 pending
- 검증용user-directive수신: 사용자 '이제 그럼 게시해' 전달, 실기경로=NAS게시후업데이트실기. **NAS게시진행수용**·검증/서명/dry-run/실게시/외부11hash·Androidpair불변/FINAL계속하도록회신. 단 '게시=push포함 상시규칙'은실제사용자제공AGENTS107-108의**gitpush여전히보류·NAS게시≠push허가**와반대라 **push만보류/기존명시철회또는push포함사용자원문확인** 요청 **seq13399**. 사용자에게재승인질문하라는요구아님, 이미있는근거1회확인. NAS/로컬ff·기록/최종보고는진행.
- rel 최초task **t-nmq8ujkx / 작업용1 / `.claude/task_rel_0162_r1.md`**. branch **rel/0.2.162 @ b4dfaedfa08e2023a495154fc59e7bda66272cbb** = 9f54912+version. Root직접blob바이트대조 product_version.hpp만0.2.161→162변경·BOM/줄바꿈보존. rel worktreeWebView2는실복사(junction없음). 아직root에worker최종report미수신.
- root직접로컬 `.claude/rel/0.2.162/windows.manifest`의payload **11개size/hash전부일치**·4exeUTF16 162각1/old0확인. 현재windows.sig없음(서명전). SetupGetAuthenticodeSignature **NotSigned**, manifest P256서명과실행파일Authenticode구분필수. 이관찰을새게시차단/새Authenticode요구로삼지않음. 실기/UAC안내서명자정확표기노트 **seq13404** 전달.
- 예정(검증용전달계획): worker dry-run보고→독립산출물검사→gnlink_release_sign.ps1(manifest서명,DPAPI개인키PC내)→remote60_verify_release/gnlink_verify_manifest→deploydryrun→NAS실게시→HTTPS/hash/Android불변→로컬mainff/docs→(push는근거확인때까지보류)→dist→FINAL_PACKET. 사용자설치/재시작/잠금/재부팅/SCM조작은자동하지않음. root제품빌드/서명/게시직접실행아님.
- **A2A inbox 신규2개발견**: seq**13402** authorization-basis(push사용자원문) / seq**13401** rel-apk-0231 user-directive+delegation('내가시킨것중작업다한거다…' preview). 둘다 `a2a_inbox_resolve(action=inject)` 성공했으나 **현재turn에서전문아직미노출**. 다음주입원문을최우선끝까지읽어push정책/APK S5범위갱신. preview만으로push허가·APK배포완료를추정하지않음. 기존push확인질문중복발송금지. inbox주입은현재보고에서응답한것아님/새동일task생성금지.

## 새 사용자 지시 rel-apk-0231 — SH포함0.2.31/PC0.2.162함께게시
- A2A **seq13401 전문도착/처리**: 사용자 '내가시킨것중작업다한거다게시해야지.마무리해서.apk에넣으라니까' 전달. **APK0.2.31/vc30에SHc494dc6포함·PC0.2.162와게시수용**, 과거S5버전/SH게시보류는이범위해소. `.claude/task_rel_apk_0231_r1.md` 전문읽고 `.claude/codex_rel_apk_0231_r1.md` 작성·검증용pin회신 **seq13411**. 실제APK taskID아직미수신/담당작업용2, directoryretry구현t-iw0926r5보다우선.
- 입력고정 **rel/0.2.162 b4dfaedfa08e2023a495154fc59e7bda66272cbb+SH c494dc6**. 새branchrel/apk-0.2.31에공유nativehello/PC기능+SHdecoder/renderer/jni보존·gate합집합확인. 충돌시그차이만검토, 동일승인범위통과면반복게시go불필요. 현재root확인때APKworktree미생성/산출물보고미수신.
- **keepalive-multi제외수용근거**: root기존codex_keepalive_multi_r1/task/featbranchhistory직접읽음. baseeedd207/공유13파일뿐아니라최대3대·park5분사용자답/최종출하판정미결로아직미게시후보. 단순재통합수고를이유로완료기능빠뜨리는것아님. 미게시과제/담당유지·'시킨모든기능완료'주장금지. 폰sealed잠금입력0.2.31에도미구현알려진한계.
- 게시APK는기존debug-key방식 noseamdebug, 이전고정APK와**전체certSHA256**일치(prefixdcc806ae만금지)/appId/version/4ABI·dex/.so gate·hello/SH native시험define없음확인. 실제APK AVD공유접속/재접속1/모니터지원Host/0.20·0.35·off/compare1 독립검사후 AndroidmanifestP256서명/--platform android실게시/외부signature/version/hash검증. APK서명과manifest서명구분·키외부금지.
- **과거최종Androidpair불변전제갱신**: Windows게시단계Android안건드림확인→승인된Android0.2.31단계pair변경/Windows안훼손→최종양채널각서명·hash·HTTPS/버전/rollback분리. crossplatform/pair원자적주장금지. S4AVD+13~15ms미달유지·폰/OS/실망/control관찰미검증, 배포완료마일스톤·실기대기이지task완료아님. 기기자동설치없음.
- 당시 **push auth seq13402**는preview만이었으나 이후전문도착·원문확인으로 **seq13414에서해소**했다(아래참조). 기존seq13399확인pending을다시열지않는다. 이번게시의검토된main+릴리스기록일반push허용. root제품수정/빌드/서명/게시없음.

## push 권한 근거 확인 완료 — seq13402 처리/이번게시 일반push허용
- seq13402 **전문도착/처리완료**. root직접 `.claude/state_2026-09-30.md`의43f5c45주변 사용자지시10-01 **'게시 = push'**→이견해소 원문과 `.claude/handoff_2026-10-21/prompt_remote_claude.md:14/17` 게시지시가있을때push포함규칙확인. 13402preview/inject미노출pending은해소.
- **이번사용자'게시해' 지시에검토된main+관련릴리스기록일반push허용**으로판단·검증용remote#wwml7h91에authorization-resolved회신 **seq13414**. 이전rootseq13399 push-only보류닫음·같은확인/승인질문반복불요. forcepush/원격삭제/무관기능전량push는허용아님.
- PC0.2.162/SH포함APK0.2.31고정산출물독립→서명/실게시/외부확인→main반영+기록→일반push→origin실해시확인/dist/FINAL 계속진행. 두채널signature/version/artifacthash·originmainhash·dist·미검증/실기행동분리. 현재root는권한근거와버전/로컬정합까지확인, **실게시/push성공FINAL근거아직미수신**. rootgitpush직접실행/제품서명/게시없음.

## LK-UX retry r4 SOURCE 완료 — 2026-10-26
- 최신 검증용 remote#yj8mrmrx의 고정 후보 **8d8655ef8cbf3a60aa0ee502e08000ac81f4cd13**, diff9ff6c15..8d8655e SOURCE OK. `.claude/codex_lk_ux_retry_src_r4.md`가 판정 정본이다. 이전 r3 N1 잔여 두 곳(Exchange end clamp, 최종 같은 mu_ 안 live+deadline+채택) 닫음. N2-a/b r3 OK 유지. 새 소스 보수 요청 없음.
- Codex 직접 diff/후보 HEAD·tracked clean/whitespace rc0 확인. relay84×4·1746ms·M10/M11 반례는 작업용 인용, 독립 실행 주장 아님. 다음 담당 검증용: 기존 LK task→R1 통합 고정 후보 독립 EVIDENCE→Codex 최종 검토. SOURCE OK는 게시/실 SYSTEM·LogonUI 성공이 아니다. 제품 편집·빌드·설치·commit/push 없음.

## capture-lifecycle r1 SOURCE — 2026-10-26
- 범위 갱신: **569059fbefda17671b8ff0a2c3695b35320f536d**(1679bde 위 로그 명칭·시험 검색·history 3파일8+/8−)까지 추가 diff 직접 확인. N1~N4 NEEDS_CHANGES 및 PS-B SOURCE OK 동일, 정본 최신 해시 갱신. UA46 재실행은 작업용 인용. 새 보수 묶음/재검사 요청 추가 없음.
- 검증용 remote#yj8mrmrx / task **t-l5o6hmjt** / 고정 **1679bde635c64e576bbcfb10105b7f8a1e6a1d6f**(8d8655e 위). `.claude/codex_capture_lifecycle_src_r1.md` 정본, **NEEDS_CHANGES N1~N4 한 묶음**. N1 desktop/source provenance와 media fence가 restart 성공 후라 옛 callback/raw/AU 경계 미충족, Unknown/의심 시 cache 연속성도 무효화 필요. N2 owner 갱신과 Take/AppliedMark 다른 epoch 읽기→새 선택 초기화/옛 작업 결산, 실제 End reset·inactive GPU0 보완. N3 Query가 pending Request를0으로 덮음+query가 pending 지움. N4 full-frame episode가 source 경계 아닌 Publish만 reset, 소진 후 새 source 회복/옛 publish budget 재무장 보완.
- PS-B 로컬 prelogin DXGI SOURCE OK. protocol90/0x100000/32B·cap gate·요청당답1·자발통지없음 방향 수용. PS/UA/LK 수치는 작업용 인용이며 고정 통합 EVIDENCE 예정. presentThread는 FrameBuffer 계수이지 렌더 아님, synthetic captureQpcUs로 fresh-source 증명 불가. duplication recreate 자동 분기 대조 필요/실 UAC·SYSTEM은 실기. 검증용→기존 task 동일 담당 보수→새 diff Codex, SH/UA2 APK 병행 가능. Codex 제품 편집/빌드/commit/push 없음.

## keepalive-r2 A spike — 2026-10-26
- 검증용 remote#yj8mrmrx / 후보 **9f1fac15fd812f4b3efe4cdec64ec385354f62f3**(569059f 위2commit), 기존 A t-y1lnbgnd·B t-x27113l6. `.claude/codex_keepalive_r2_spike_r1.md` 정본. admission/activation 분리 GO, SOURCE NEEDS_CHANGES S1 replay=deviceId/gen만으로 임의token·주소에 secret재발급/20s만료vs30sreplay, S2 active에서gen감소·0sentinel로 늦은재탈취, S3 actualEnd/table owner 미연결·난수실패무시, S4 공유relayendpoint의 parkedcontrolgate 붕괴/relaymock 승인전lastActivated.
- 결정: 실제relay는 클라이언트별lease+별도host-facing소켓 ⓐ, 최신token1개규칙·49BHello전용bind/provisional·역방향index/NAT경로/유한상한 함께 변경. ⓑ 단순standby demux 제외. wire311~316배정·8entries/20slease/status0~3 GO,104B/activationAck 상관은최종미동결. 기존UDP평문bearer 수준 조건부수용(기밀성주장없음·재사용자격수명확대명시), 인증우회/난수실패/종료철회보완필수·범용암호화계층R2필수추가아님.
- ②격리shared/API·APK준비 조건부GO, host보수+핵심독립확인 전 자동preconnect출하OFF/운영relay변경없음. 작업용1 capture-lifecycle/R1 우선→KA, 작업용2 분리APK준비. R2=PC164/APK33·폰전면기본. 25/72×2는작업용인용, 독립/실relay/heldrelease미검증. HEADclean/diff직접확인·제품빌드/서버/commit/push없음.

## capture-lifecycle r2 SOURCE — 2026-10-26
- 고정 **6fa709a65a8a940d0b045dc615925ba442ac7872**(569059f 위), 정본 `.claude/codex_capture_lifecycle_src_r2.md`. SOURCE NEEDS_CHANGES 잔여 N1-a 실제Restart는Detach가attachmentDesktopGen을성공전current로만들어실패후stale=false/desktop전용카운터탈출→wrapper restartPending/일반tick의8s무한backoff가3회상한우회(현seam은함수전fail이라못검출), N1-b GDI meta.cookie0우회·Unknown전늦은원본재채택, N2 Endreset뒤sameepochpending재적용, N3 host packed먼저/handled나중별도atomic읽기로false-applied/torn snapshot.
- 닫음: N4source-keybudget·자체WGC재획득구분, N3clientQuery분리, N2새owner요청보존/ownkey·inactiveGPU0, N1edge선행fence·nonzero cookie/PS-B. M9원문ALLPASS32/0·r2_d두PS각blank3회/content0확인→비결정분류타당/변이실패주장금지, limited deterministicpointer→image대조는통합EVIDENCE. 보고수치는작업용인용, Codex실행없음. 기존t-l5o6hmjt 동일담당한묶음보수→새diff, R1통합독립검사예정. 제품빌드/commit/push없음.

## capture-lifecycle r3 SOURCE — 2026-10-26
- 후보 **6ffd94bc7dca53e1874a9401bf336d8d7b7b5d5a**(6fa709a 위), `.claude/codex_capture_lifecycle_src_r3.md` 정본. **N1-b/N2-end/N3-host·N1-a DesktopRebuild SOURCE OK**, N4/clientQuery/PS-B 유지. 유일잔여 **FullFrame owner 실패retry가 stage_time_limit에서무제한**: DesktopRebuild만제외, FullFrameAsks는최초1만증가·일반tick은budget차감없고8s계속retry. fullFrameOwnRestart로boundary보존만으로상한안됨. 기존t-l5o6hmjt 동일담당한곳보수, Generalgeometry회복전역상한변경불요.
- finalPS-pointer34/0와M9 2FAIL, desktopDetach실패21/0(동일gen2회차성공/3회포기10s이후0)·M12/M13FAIL, M10/11/14/15FAIL저장원문직접확인. 모두작업용실행열람이며독립PASS아님. pointer대조는분기증거/실UAC픽셀렌더미확인, R1통합독립EVIDENCE예정. 제품빌드/commit/push없음.

## capture-lifecycle r4 SOURCE 완료 — 2026-10-26
- 최종 고정 **adc948212c7cefaa58c300909ae1f426d0805f9a**(6ffd94b 위), `.claude/codex_capture_lifecycle_src_r4.md` 정본. **SOURCE OK**, 마지막N1-a FullFrame닫음. 일반retryGeneral만/자기FullFrame실패는같은episode ask2·3으로실제결산·backoff/1s/상한유지·새source새budget. 이전N1-b/N2-end/N3-host/DesktopRebuild/N4/clientQuery/PS-B OK유지. 추가소스보수없음.
- finalr4uaff18/0: ep1ask1fail→2ok,ep2실제Detach뒤3fail/12s추가0,epoch2ep3ask1ok 직접로그대조. M16일반retry복원6회·18/3FAIL·회귀21/32/26/54rc0요약직접확인. 작업용산출물열람이며독립PASS아님. 다음검증용 R1 LK+capture+SH/UA2고정통합→독립행동/출하gate→Codex최종. 현재SOURCE승인≠통합EVIDENCE/게시/UAC실기성공. WGC_STILL은시험define안·통합표식gate추가. 제품빌드/commit/push없음.

## rel-0163 EVIDENCE r1 — 2026-10-26
- 후보build **90192f49ffee4d6dd56ea1db1552399de8999258** + gate/docs **66bb5e92b0225f7cefbb38a89a702bf9247ebe65**, 기존rel task **t-bpwzezfg**. `.claude/codex_rel_0163_evidence_r1.md` 정본: **통합SOURCE/산출물OK, EVIDENCE E1 실제renderer자동행동 보완 전 보류/실게시·mainpush대기**. 권한재질문아님; user10-26게시/push승인유효. 같은후보제품변경없는PSstill picker10s→actualpresent≤1s·정지갱신, 이미WGC samecontrol 2회secure/Default전환새source→실render/marker·스크린샷 최소묶음필요. 현재PS/UA/realrecv는FrameBuffer계수/lastPresentedsetter·telemetryrender0이라actualUI완료근거아님. 이미r1~r4조건이므로새요구아님. 기존동작전량재시험불요.
- Root직접 mergeparentsmaina1c551c+adc9482/productdiff0·version치환만/BOMcheckout1020CRLF17·90192f4→66bb5e9apps/libs0·trackedclean확인. manifest5ea1ac7453465829a369498f795929e11dd7050dcc409b5f96e92e484f60a476/SUMS9b1a21dd98b831152f342a8e29c9b7be53e81a0872c5664b16affccbc3c172b5·11payload크기/hash·버전UTF16일치. ship10vcxproj+capture시험define0, 읽기전용gate직접9PASS, verifier시험exe5hash일치. 독립PS32/UA54/fullFrame18/LK68·79/realrecvPASS원문확인(실행주체검증용). root제품빌드/실험없음.
- 작업용DWrunner screenshot4-restored직접열람해stream확인/worker실행귀속. 영속quarantine 'not recorded folder-unresolved err3'는실패이며runner는record시도만PASS, 영속쓰기/재부팅성공주장금지·fixture원인후속원장. PC먼저/APK32나중수용·PC때Androidpair불변필수/SH·KA·NP미포함. E1보완→Codex→서명/gnlink배포·외부검증/main+docs일반push/dist/FINAL, 현재사용자조치없음.

## rel-0163 E1 제품 blocker 결정 — 2026-10-26
- report_rel_0163_r2_e1_blocker.md 전문+PC picker/SelectionGate/allowlist/Monitor응답/sharedRequestDesktop 소스대조. 실제renderer3/3FAIL: prelogin desktopclick→sel.BeginawaitAck+WindowSelect, 알려진Prelogin이allowlist에서버림→DropAwaitingAck/picker영구/present0. 10-22로그일치정황/당시Host0별개라단일원인확정금지.
- 정본 `.claude/codex_rel_0163_e1_decision.md`: **0.2.163필수포함**, (a)보강 **known PreloginV1 desktop0만 로컬ackless선택transaction** 채택. allowlist/input/sealed경계유지·일반WindowSelect확대없음. pending/op epoch/첫IDR+현재연결gen학습/UIreveal재검증/실present보고유지, 클릭즉시hide/0gen영구pass/가짜hostAck불가. role미확정첫click유예1회·늦은op/reveal/역할변경fence, UAC/LockCred만으로발동금지.
- (b)기존MonitorSelect는mailboxpost즉시MonitorList(세대/적용완료없음)라ack대용불가·preloginDXGI/WGC차이,새protocol확대안함. shared/APK같은ack대기확인·tag/incarnation유지 APK32정본에필요보수취합,PC우선·APK미착수에묶지않음/native한writer. 기존t-bpwzezfg작업용1→새productcommit·재build/dryrun·freshhash/gate/독립실renderE1→Codex. UA E1독립계속. 버전163유지/old90192f4manifest재사용금지/게시보류. 사용자조치없음·root제품수정/build/push없음.

## rel-0163 picker SOURCE r3 — 2026-10-26
- 제품 **57298805dd782b0e246315c7258f5f986cb23551**, diffb8326ec..5729880/시험9b836e2, `.claude/codex_rel_0163_src_r3.md` 정본. SOURCE NEEDS_CHANGES R1 rolefalse읽기→첫pongdeferred0→UI등록lostwake, R2 unsendable Aaction에post시currentBepoch붙임, R3 BindLocalGeneration에op/run없음·CAS0 ABA와post시liveepoch재부착·FrameGate/decode전headerbind, R4 ApplyAcklocal구분없고panel도먼저변경/시험이lateAck를Acked로기대해계약반대.
- 올바른knownPreloginV1 desktop0분기·allowlist유지·실renderer배선/33PASS방향수용. 보고275/279ms·present는작업용인용/독립EVIDENCE아님. 기존t-bpwzezfg 동일작업용1에R1~R4한묶음·제한반례→새diff SOURCE→새build/dryrun/hash·독립renderE1→Codex→게시. APKshared동일문제확인/보수APK단계,PC우선. 이전LK/capture SOURCEOK유지·root제품편집/build/push없음.

## rel-0163 picker SOURCE r4 — 2026-10-26
- 제품 **501f7061b95f19d9b86e633610b214f6329ccf1e**/시험8919ec2, `.claude/codex_rel_0163_src_r4.md` 정본. R1lostwake/1회실행과R2action원tag닫음, mutex/opBind·decode뒤bind·IgnoredLocal방향수용. SOURCE NEEDS_CHANGES 잔여3: R1-run role read뒤liveRun으로intent재부착(Aclick→B재연결사이등록), R3 live검사뒤최종FrameBuffercommit틈/취소후localfalse검사우회+유효출력검사전bind, R4 localprecheck→UIlocal시작→panel先변경→gateIgnoredLocal로panel은이미오염. 세곳짧은최종permission경계/limitedlatch대조필요.
- 기존t-bpwzezfg 같은담당한묶음·닫힌부분재검토/PSUALK전량반복불요. SOURCEOK뒤재build/dryrun1회/freshhash·독립E1→Codex→게시계획유지. 보고proc51/PS33/UA28은작업용인용,root실행없음. 제품편집/build/push없음.

## rel-0163 picker SOURCE r5 — 2026-10-26
- 제품 **7d82c86cc6e98019a5e667d6d181c4f11e56f2a4**/시험f92e4b6, `.claude/codex_rel_0163_src_r5.md` 정본. R1-run/R4-panel SOURCEOK·R2유지. **R3만NEEDS_CHANGES**: CommitFrame은gate.mu를return시풀고그뒤framewrite→UIBegin/Clear와직렬화아님(실writecallback/permission수명유지필요). pendingfalse+activeGen非0+같은epoch예외는oldactive7→새A취소→Agen9를이미reveal로오판허용, actualactiveOp+gen구분필요. 원frameLocal/op취득일관성도그permission내유지.
- PS35/UA28저장요약확인·작업용인용, 새PS(4)는refusedcounter만assert하고versionAfter불변은assert없음(출력뿐)→Awritecallback/전이순서limited대조필요. 기존t-bpwzezfg R3두곳만보수·다른닫힌지적재개/전량PSUALK반복불요. SOURCEOK뒤1회build/dryrun/freshhash·독립E1→Codex→게시. root제품편집/build/push없음.

## rel-0163 picker SOURCE r6 — 2026-10-26
- 제품 **5ce3ce744d103df4ff6f15fae54aa7f91335534b**/시험384b377, `.claude/codex_rel_0163_src_r6.md` 정본. 이번R3두보수(write끝까지unique_lock/revealedOp성공기록)SOURCEOK. FrameBuffer→gate제품역순없음(ApplyHostAck gate→panelmodel뿐·renderergate요청없음)source감사완료.
- 유일잔여 R3초기ticket: frameOp=dec.recvSelectionEpoch 후별도IsLiveLocal(frameOp), 사이A취소/Bbegin이면false가돼CommitFrame(!frameLocal) ordinary즉시permit으로op검사우회. r5말미frameLocal/op일관성조건미완. 같은gate잠금snapshot/ticket으로epoch+mode취득→decoderreset/최종commit/reveal까지고정보존·그경계반례필요. 기존t-bpwzezfg한곳보수·닫힌지적/전량시험재개불요. PS37/UA28/M4M5작업용인용·root실행없음. SOURCE전체NEEDS_CHANGES/게시보류.

## rel-0163 picker SOURCE r7 완료 — 2026-10-26
- 제품 **eefac097791bfc0cbfefff60aba2b89d41cbdf38**/시험 **0854ac49a7cba1f9ce8a4055b2f9813fc41fb6dd**, `.claude/codex_rel_0163_src_r7.md` 정본. **SOURCE OK**, 마지막R3ticket닫음. gate한snapshot(epoch+local)→그epoch decoderreset→동일ticket CommitFrame/reveal, ordinary→newlocal혼합거부. 기존R1/R2/R4/writeunique_lock/revealedOp 및LK/capture SOURCEOK유지. 추가소스보수없음.
- PS39/version83→83·B완료/UA28/M6 83→85FAIL/M7gateFAIL은작업용보고인용,root실행없음. 기존t-bpwzezfg→최종제품+시험고정→재build/dryrun1회/freshpayload·manifest/hash/gate/compile/hook출하감사→검증용독립실rendererE1·관련smoke→CodexEVIDENCE。그후기존승인서명/gnlink/외부검증/main+docs일반push/dist/FINAL。과거EVIDENCE보류자동해제아님·old90192f4hash재사용금지·PC163먼저/APK32별도·KA/NP R2미포함。사용자조치없음/root제품편집/build/push없음.

## rel-0163 EVIDENCE r2 최종 후보 승인 — 2026-10-26
- 최종build **fae0e1fc787456e852af37af35de7b19f87f01ac**(제품eefac09/시험0854ac4/docs), `.claude/codex_rel_0163_evidence_r2.md` 정본. **SOURCE/EVIDENCE OK, r1 E1보류해소·서명/gnlink/외부검증/관련main+docs일반push/dist GO**, 사용자10-26기존승인·추상go재대기불요. 아직실게시완료/실UAC성공아님.
- root직접11payload크기/hash·manifest **6d89ae487a125ebc620e46e6be5396cb3131dc6380878b596a85b47edf64dfd3**/SUMS **2a3f5163279d3e07d67b1ad6051cf59dc966f30548970c69961bdda1c49d6c0d**·버전163/old1620·시험6exe현재hash일치·ship10+capture시험define0·hook제품대입0·gate직접9PASS확인. 제품최종diff0/trackedclean.
- 검증용독립E1 PS39(실카드present287/297ms·3s4~6present·ticket취소version84→84/permission대기·실picker→원격화면)·UA28(같은WGC2왕복adopted=presented/new색100%old0)원문확인, rootps_picker/ps_presented/ua_green이미지직접열람. LK68/79·UA54·FF18·realrecv출하StreamPASS; 실행주체검증용/Root로그검토. 첫pong4s지연fixture3494ms는≤1s표시에합산안함. 실SYSTEM/Winlogon/UAC/LogonUI·회사PC/폰미검증유지. DW영속기록실패별도ledger.
- 고정Stream825feb97a067b7b7841a5317fca220c7b6fa59bb917412dc9cc96ed8a5791b35/Viewer0d47c4ba9daf038034c5fcf396a8dd5215e4aca41972e5c118ba6a8d0bd1d81f/Setup907e5702ac744c558e023f11d9751a8f7e2a3d6d5795123cbb2729ab8979cc1d. 다음검증용 기존t-bpwzezfg 서명→변조거부→deploydryrun/실게시→외부11hash/서명/버전·Androidpair불변→main/docs일반push/remotehash→dist/FINAL→Root최종보고. PC먼저/APK32별도·KA/NP R2미포함. 사용자조치없음/root제품build/서명/게시/push직접실행없음.

## PC 0.2.163 FINAL 완료 확인 — 2026-10-26
- 검증용FINAL_PACKET처리, `.claude/codex_rel_0163_final_packet.md` 정본. **PC163 배포완료(마일스톤)·실기대기**. buildfae0e1f/main+docs **5970f046f56dd8f55b98b4ab971ed7376c6d5848**, root직접local/origin/ls-remote동일확인. docs3파일제품변경0.
- root직접signedmanifest6d89ae487a125ebc620e46e6be5396cb3131dc6380878b596a85b47edf64dfd3/sig57d1d2ca5b34568e580ea0676504e4aed7583610870b2b0f50f4716a0174d01a·JSverify正/내장변조거부rc0확인. **공개HTTPS11 URL별도GET status200/size/hash11전부일치**. distSetup907e5702ac744c558e023f11d9751a8f7e2a3d6d5795123cbb2729ab8979cc1d=payload,URL https://gnlink.shotan.net/updates/0.2.163/GNLinkSetup.exe. C++정상/변조와서버디스크pair/Android309a9c5e/cbb5b7d6불변은검증용직접보고귀속. deploylog서버11/백업old162pair/실게시/external11PASS확인.
- 사용자다음:호스트/뷰어163업데이트→같은연결UAC창/예·아니오복귀실기. 재부팅preloginpicker/실LogonUI비번retry·폰WGC뒤PCDXGI·회사PC미검증. 자동설치/재시작/실UAC조작안함. APK32미완료(작업용2오프라인),R2KA/NP별도(작업용1 S1~S4+relayA). root최종사용자보고필수/PC완료를전체완료로확대금지. Codex직접제품build/서명/deploy/commit/push없음/중복공개기록commit없음.

## KA host/relay r3 SOURCE·설계 — 2026-10-26
- 고정933814f(실worktree93b15f6는docs1줄만/appslibs동일), `.claude/codex_keepalive_r2_spike_r3.md` 정본. SOURCE NEEDS_CHANGES N1 host-facing최초pin이같은IP첫packet만믿음(hostPunch.authToken0/hostOnlytoken은pin대조안함)·host이동재pin도proof필요, N2-a 인증replay판정FNV64축약→crypto digest/failclosed, N2-b parked새admitGen11뒤unusedcap구gen10이교체가능→generation/incarnation순서보호, N3 standby8뒤legacy9할당허용→양쪽상한/비파괴거부·env정수검증.
- 결정1 activationPong entry/gen/status/Granted GO(역순/손실/cancel·bareHelloAck완료금지·admission≠영상). 결정2 additive standby API/relay상태방향conditionalGO, IP최초pin미승인/Host최소proof확장가능·oldDirectoryfeature를부작용없는metadata로선확인. 결정3 host lease당clientUDP socket1 GO(다른TCP제한뜻아님). sourceS2/S3기본보수·S4별도socket무간섭방향수용·계정close全sessions회수확인. table47/e2e89×2/dirsuitALLPASS저장원문확인·작업용인용/독립실행아님·K4등가/R5생존분류수용.
- 기존KA A t-y1lnbgnd정본에N1~N3한묶음, 작업용1다음은**R1 APK32우선**그뒤KA보수. 자동preconnect출하OFF/NAS무변경/R2NAT·권한포트·비용·heldinputrelease·실APKPC미검증. PC163게시영향없음/root제품build·server·deploy·commit/push없음. 사용자조치없음.

## APK 0.2.32 prelogin 선택 SOURCE r1 — 2026-10-26
- 고정 fix/apk-prelogin-select e8123e4(제품 627acdb, 시험 c43effd, base main5970f04), `.claude/codex_apk_prelogin_select_src_r1.md` 정본. SOURCE **NEEDS_CHANGES N1·N2 한 묶음**. N1: queued A sink BeginLocal 성공 뒤 UI 취소/B 직접 local·tracker B 게시 → A Convert(panel B) false 무시·tracker A 덮음 → poll이 0으로 지워 B panel 완료 유실. N2: unbound local pending IDR G 실패 → 다음 H 첫 Pump G 실패 → H 신규 queue 성공·bind H → 남은 G가 gate 없이 재queue·출력 H로 표기 가능. 실제 APK 재현 아님/소스 순서 근거, limited latch·queue 경계 반례 필요.
- PreloginV1 desktop0 한정·원 action tag·첫 Ping 우선+panel queue·allowlist 유지·sink 전체 잠금 방향 수용. PC FrameBuffer ticket 복제 요구 아님. Worker 38/37/249·빌드는 보고 인용, MediaCodec 없는 Feed 하네스로 N2 검증 불가. AVD 실제 UI는 SH/UA2 통합 EVIDENCE에서 유지. 기존 APK32 같은 담당 보수·변경 diff SOURCE→고정 통합후보 독립 AVD/gate→Codex→기존 승인 게시. PC163 게시 재개방 없음/KA 후순위 유지. root 제품 편집·빌드·실행·commit·push 없음/사용자 현재 조치 없음.

## SH strength r2 A/B SOURCE·옵션 결정 — 2026-10-26
- 고정 **6d5c032**(A3dc15c7/B2b5a718/시험6d5c032), `.claude/codex_sh_strength_r2.md` 정본. **A 강.60 SOURCE OK / B 사용자 선택 옵션 구현 GO, 기본 A 유지 / 현재 B 출하 SOURCE·EVIDENCE 미승인**. 이름은 '화면 크기(실험)' 등, OSLink 동일 방식 이름 금지. 강도/default/off 저장 보존·제품 설정 분리/seam gate 유지. 실폰 S4·발열·체감은 게시 후 실기 수용(실험 옵션 한정), 기존 AVD 미달 삭제/기본B 전환 아님.
- root 저장 JSON·fixture/하네스·전체diff·이미지 ab_text/lines/checker 직접 확인, AVD 재실행 아님. ROI edge B.35/A.35+37.38%, A.60/A.35+4.47%·A.60/A.20+9.96%; 가독성/실폰원인 확정 아님. 12개 window p95 중앙 off87.160/A96.418/B108.099ms(전체p95 아님). noseam9c67f8d0…/seam95ae4996… 해시 직접 확인. APK 비교는 별도 재접속 정적 ROI, encoded same-frame 아님; fixture가 결정적 동일입력 근거. worker26/14/36·249/gate 인용.
- B 보수 N1 첫 bind pathNONE source buffer→STARTING B view placement/identity가 다음 sync resize보다 먼저 그려질 수 있음; actual output/layout/surface snapshot·첫표시/입력 fence 필요. N2 매 status poll 동일 placement setter→GL post/redraw 반복(로그18redraw/5s), 동등값 무작업·최신 한 작업 합류/정지pan 최종redraw 유지. 현재 실APK 재현 아닌 소스순서/저장로그 근거. 보수 후 비용 1회 재수집.
- 같은 APK32 담당 정본 취합→B 제품설정/전환·회전resize·정지pan/host좌표모서리/letterbox무입력·crop/rot fixture·움직임shimmer·failure/direct/producer 수명→고정통합후보 독립검증→Codex→기존 게시. pickerN1N2 별개 진행/PC163 재개방 없음/KA후순위. 사용자조치없음/root 제품편집·빌드·AVD·commit/push 없음.

## UA2 APK SOURCE r1 — 2026-10-26
- 고정 **5f73ad1**(제품 **4df7db7**, diff6d5c032..4df7db7), `.claude/codex_ua2_apk_src_r1.md` 정본. SOURCE **NEEDS_CHANGES N1~N3** 한 묶음. N1 requested==asked만으로 처리완료 추정: prelogin WGC/GDI 거부 후 requestedDXGI·handled/NotYet0에도 영구APPLYING, 반대로 requested같은NotYet도 APPLIED. 원 intent/응답 상관·pending/거부terminal 구분. N2 WINDOW_TARGET인데 dormant desktop activeDXGI를 '실제 적용 DXGI'로 점등; 요청GDI저장과현재창WGC 분리. N3 공통hint가 구Host에도이번연결/다음기본보장, capabilityunknown/unsupported구분없음·SESSION_SCOPED 미사용→지원확인과정직한legacy안내 필요.
- root 고정9파일·시험8/AVD코드·ua2_7 tap/Host로그·after_pick이미지직접확인, 실행은worker귀속(13/257/회귀). 실제Host seq7 req3/effective3/active1/flags5·desktopActive0 stored→종료reset→새epoch기본DXGI 확인. GDI1건전달유효/desktop실전환 증거아님. global저장삭제/자동재전송제거/새연결reset/Query가pending변경보존/park보존/GDI1..3 방향수용. 조회간격실제2.2~2.3s라≤2s보장대신약2초로보고정정(새타이머요구없음).
- 같은 UA2 **t-ui7wvkxk 작업용1** 정본취합→제한보수·해당diff SOURCE→고정APK32 독립UI/legacy/역순/Host교차·전파0→EVIDENCE→기존게시승인. picker/SH 미결별도유지·PC163재개방/새병렬writer없음. root제품편집·빌드·AVD·commit/push없음/사용자조치없음.

## KA host/relay r4 SOURCE — 2026-10-26
- 고정 **f4ddc9a**, diff933814f..f4ddc9a/Host7d0a10c·server7ff92d5+9bbff8a·testf410f7d. `.claude/codex_keepalive_r2_spike_r4.md` 정본. **제품 SOURCE OK, r3N1~N3 닫음 / ② 클라이언트 구현 GO / NAS·자동preconnect출하 OFF**. 예약별 hostProof 최초pin·Host만heartbeat전달/목적지punch/proof대상cap미등록/이동freshproof, SHA256전체·digest실패auth0, 같은device gen≤현재Stale/Superseded·entry보존, legacy도8개비파괴거부/교체8·env정수fallback, read-onlyhosts standby metadata/성공connect 구체relay이유 수용. 기존결정1/3유지·32bitcounter재시작/내구예약·역순/유실/cancel②계약명시.
- **N4 시험 실행 경계 보수필수**: 새 relay_env_bound_test.js os.tmpdir/remote60-envbound…json→fs.rmSync, 기존 test/run.js도 os.tmpdir rm. AGENTS 최상위 root밖삭제제한 위반 실행 경로. root는실행/삭제안함. suite완료저장로그상worker경유코드실행으로판단/정확경로·실제삭제사실은검증용확인·사용자보고, 추가바깥정리금지. 임시루트 D:/remote/remote/.claude/test-tmp/고유task로runner+필요child연결/최종실경로검사→안전범위내정리. 제품rebuild불요/blob동일고정exe유지. 무관전체suite대수술/바깥삭제재허가불요.
- root 전체12파일·계약·table56/e2e89×2/suiteALLPASS원문·새테스트읽음/whitespace0/HEAD고정trackedclean. 실행/7mutant는worker귀속/독립PASS아님. proof응답/Punch유실 때deadlease bounded회수·held input release·실NAT/서버LAN/포트/권한rollback·실APKPC/장기비용후속. 같은writer APK32 picker/SH/UA2미결우선·KA선점/새병렬task없음. root제품build·server·delete·commit/push없음/사용자현재조치없음.

## APK32 picker SOURCE r2 — 2026-10-26
- 제품 **86df4cd**·시험 **1de9dc7** 두 commit만 immutable 검토, `.claude/codex_apk_prelogin_select_src_r2.md` 정본. **NEEDS_CHANGES 잔여N1 poll/Abort 최종경계 1곳**. localSelectMu가 controller start/convert/poll끼리만 보호; JNI Prepare/Abort는 sink 직접호출→poll Current/Bound G 읽은 뒤 Abort 완료→panel currentTagA 그대로라 AApplyLocal ACKED 가능. poll bound읽기→commit전 latch 취소/새Prepare 반례 필요/끝까지동일소유 경계 또는JNI취소/Prepare동일transaction·lock역순주의. P8은BeginLocal/IDR전latch라다른경계.
- N1원래trackerA→B덮기는닫음: 같은localmutex+Convertfalse미게시. N2 **SOURCE OK**: 실제Android Offer/Pump가 제품DecoderPendingSlot/PermitsQueue callback까지sinkLockedState내, ahead금지/IDR교체/delta rekey/bootstraphandle유지. root저장slot10PASS/mutant7FAIL200100직접읽음/실행worker귀속. 공유제품경계대조수용·MediaCodec고갈seam을새필수추가안함, 고갈실기미증명/최종실APKdecoder UI독립은필수.
- 같은브랜치 UA2r2cf8c021/41de4cf·SH Kotlin WIP제외/보존, 현재HEAD41de4cf. 같은picker담당 잔여N1한곳→해당diffSOURCE→고정APK통합독립EVIDENCE. PC163게시재개방/새writer없음. root제품build/실행/commit/push없음·사용자조치없음.

## APK32 UA2 r2·SH r3 SOURCE — 2026-10-27
- 고정 통합HEAD **05fe4b9**(picker최종미포함/출하아님), UA2제품 **cf8c021**·시험41de4cf, SH제품 **2f6905d**·시험05fe4b9 각각전체diff/호출/시험검토·whitespace0. `.claude/codex_ua2_apk_src_r2.md`와`.claude/codex_sh_strength_r3.md` 정본 둘다NEEDS_CHANGES. 직접AVD/build없음/WIP제품편집없음.
- UA2 N1 seq/intent·terminalprelogin·N2창WGC/desktopstored분리·구Hostscope문구는수용. 잔여N1 requested==asked+handled만으로 '적용됨': desktop paused저장/실restart실패 requested/effectiveGDI activeDXGI에도GDI적용됨, asked없음0필드NotYet도IDLE→actual/처리분리·unknownchecking. N3 JNI answersbool먼저/known나중별도읽기→첫Pong사이에knowntrue+answersfalse로새Host구Host오판가능, HostRoleGate한Load flags/knownsnapshot활용. unitoldA requested동일seq3<latest5로seq변이검출보강(현재lateB불일치case는r1도pass). ua2r2_2GDIstored이미지/로그직접열람/실행worker귀속.
- SH N2동등placement양단dedupe/최신post한개SOURCEOK·JSONglRedrawsA0B0확인, 기본A/제품B저장off/seam분리수용. 잔여N1 a `!matches && clearSwaps<2`라2회후mismatch영상draw통과/clearSwap실패미처리 b 기존정지frame draw(false) mismatch면frameWaitsfalse→retry미실행·dedupe로clearblack뒤재개없음 c 새view치수만있고GL채택/최종latch→UI입력layoutfence없음,옛화면새좌표tap가능. 하나의layout소유/대기재개계약으로제한반례3개필요.
- method16 UI실화면b3_method직접열람/worker실행, fixtureBcrop/rot≠Activity회전, touch36중앙블록≠모서리letterbox, latency6로그수집≠성능PASS. 현재자동화로정지resize/pan/fallback/shimmer까지다했다는보고금지/빠진자동행동통합후검증. 비용sample segmentoff88.070/A100.029/B110.595ms≠windowp95중앙85.1/98.9/110.6. 같은writer/각정본잔여취합→SOURCEdiff→최종고정APK독립EVIDENCE, picker별개진행/PC163재개방없음/사용자조치없음/rootcommitpush없음.

## KA r5 N4 시험 guard SOURCE — 2026-10-27
- 고정 **357ee5e**(test7파일만/제품0), `.claude/codex_keepalive_r2_spike_r5.md` 정본. **N4 NEEDS_CHANGES a/b**, 제품r4SOURCEOK·②GO/NAS·자동preconnectOFF 유지. a helperroot가 .claude/test-tmp/task shape만검사→C:/work/other/.claude/test-tmp/ka도허용해root밖삭제가능; mkdir전실경로없음·root/ancestor링크도shape같으면허용. trusted주repo D:/remote/remote/.claude/test-tmp anchor·생성전ancestor/후실경로검사 필요. b 수정child4개(account_admin/device_credential/recovery_fault/relay_wire)는 rawenvpath.resolve→mkdir/mkdtemp·미설정Cworkfallback이라helper우회; 같은검증entry로통일. 무관UIrunner전량재작성요구없음.
- root 전체diff·호출/cleanup·whitespace0·제품diff0확인, suiteALLPASS저장읽음/815·TEMP변화0·부정대조는worker귀속. shape맞는바깥root/링크와childstandalone무env·무효root가첫쓰기전거부되는제한대조요청/바깥폴더생성·삭제실험추가금지. 정상root개선수용, 전체815매라운드반복·제품rebuild불요.
- 과거TEMP삭제실행pid/횟수미기록/동명기존파일가능성기록유지·현재TEMP0≠과거피해없음. **scratchX** C:/work/gnlink-wt/ka-r3/scratchX root읽기확인일반빈폴더0항목·삭제안함/사용자판단자료. 새바깥cleanup/허가질문불요. 같은KA정본N4만보수하되작업용1현재picker최종우선/선점새writer없음. root제품build/suite/delete/commitpush없음·사용자현재조치없음.

## APK32 picker SOURCE r3 완료 — 2026-10-27
- 제품 **9afcec7**·시험 **3a17444** immutable전체diff·Android/JNI/model/호출읽음/whitespace0·HEAD3a17444 trackedclean. `.claude/codex_apk_prelogin_select_src_r3.md` 정본. **제품 SOURCE OK / N1최종닫음**, r2tracker/N2 queue OK유지. 실제CommitLocalSelectionFor가Prepare/Abort동일LockedState안 tag/bound판정→callback CAS/panel기록, local→sink→panel/역호출없음/끝난뒤local해제→sessionmu. 취소뒤새ACK없음·기존먼저기록된panel과거상태잔존과구분, sinkpending/ready무효.
- root저장psel_r3/after56PASS·mutP9 2FAIL 원문읽음/실행worker귀속. P9변이atuidone0→selected vs보수atuidone1→selected로순서대조. 실제Android경합재현아님·최종실APK AVDEVIDENCE별개.
- **E1시험helper누락** viewer_prelogin_login_e2e_test.cpp --android-select GateSink에새Commitoverride없음→기본Abandoned/tracker삭제, 옛37PASS재사용불가. 같은자체mu callback배선 시험전용추가·그target만재build/실Host행동확인, 검증용이시험diff/EVIDENCE검사로연결(명백한helper보완에제품SOURCE재왕복/전체PC빌드불요). 제품기본fallback옛read형복원금지. UA2r3/SHr4보수다음→새고정통합APK독립→CodexEVIDENCE→게시. PC163재개방/새writer없음/사용자조치없음/root제품build실행commitpush없음.

## APK32 UA2 SOURCE r3 완료 — 2026-10-27
- 제품 **482a901**·시험 **3e4d567** 각각전체diff/호출/Host상태계약·테스트읽음/whitespace0·HEAD3e4d567 trackedclean. `.claude/codex_ua2_apk_src_r3.md` 정본. **제품 SOURCE OK N1/N3잔여닫음**. requested같아도active다르면RECEIVED terminal/원인추정없음, effective대체=active는POLICY_REPLACED, active요청같을때만APPLIED·windowstored/거부기존유지; asked없음0/NotYet는CHECKING. capabilitygetter HostRole한Load known+flags→JNI한snapshot/출하stopPoint인자없음.
- root저장native60PASS/P10변이1FAILknown1answers0/Kotlin266중3FAIL원문·ua2r3stored흐름읽음/실행worker귀속. oldA같은requestedseq3<sentSeq5 단위보강수용. 창AVD13≠desktopIDLE UI/실backend전환, source기본DXGI존재≠EVIDENCE. 최종안전한fixture/prelogin통합독립에 desktop상태없음→CHECKING→IDLE/접수실제표시 제품UI확인묶기/유저화면임의캡처없음.
- picker제품9afSOURCEOK유지/E1--android-select helper는기존seq14189시험보수/EVIDENCE(제품재승인왕복불요), SHr4현재미결보수후새고정APK→검증용독립→CodexEVIDENCE→기존게시. PC163재개방/새writer없음. root제품build실행commitpush없음/사용자현재조치없음.

## KA r6 N4 guard SOURCE 완료 — 2026-10-27
- 고정 **cf8fb70**(test5파일/제품0), `.claude/codex_keepalive_r2_spike_r6.md` 정본. **N4 SOURCE OK a/b닫음**. trustedD:/remote/remote/.claude/test-tmp anchor exists/nonlink/realself→candidate부모동일·단계mkdir→후realparent/링크검사. child4 root/scratchDir공유·rawenv/worktreefallback제거·recursive cleanuphelper. 일부고유scratchflag rawunlink/rm잔존하나검증root내고정경로라수용/전량기계교체요구없음. r4제품SOURCEOK/②GO/NAS·preconnectOFF 유지·rebuild불요.
- root전체diff/runner호출·whitespace0/HEADtrackedclean·anchor일반디렉터리읽기확인. **직접비변경NodeVM guard3사례**: 미설정/shape맞는Cwork바깥root/anchor자체 모두거부·fs mutation호출0(실mkdir/delete전부차단wrapper, 실제생성삭제없음). junction/child무env/긍정recovery·113·83·relayfixed는worker귀속/815재실행불요. 실제서버suite독립성공주장아님.
- N4닫고검증용안전runner→기존KA핵심독립검사연결/새guard승인왕복불요. 같은writer APK32최종통합AVD우선·KA선점없음·proofloss/counter/socket/heldinput/NAT/비용미검증그대로. 과거TEMP삭제pid/동명기존파일가능성기록유지/과거복구성공주장금지·scratchX보존/새정리허가대기불요. root제품build/server/suite/delete/commitpush없음·사용자조치없음.

## APK32 SH r4 SOURCE — 2026-10-27
- 제품 **9886c22**·시험 **0c48c6f**·HEAD **7795289**, .claude/codex_sh_strength_r4.md 정본. **NEEDS_CHANGES N1-c1/c2 한 묶음**, a mismatch유한retry/direct·b무새frame redraw·N2dedupe수용. c1 Activity latch timestamp후latest swappedPlacementGen읽음→old A latch callback 지연중 B swap이면 B아직미표시인데shownB/open입력. 보고한 한버퍼빨리는허용안함/상한도미증명. clear체크전shown갱신·lastFrameTS재사용clear가영상V미latch후먼저latch되면new영상/firstgate계수가능. 실제output별token/metadata(kind/renderer/surface/layout+원decoderframe)상관/원handoff지연·redraw분류/metadata-beforebuffer·실패withdraw·bounded필요.
- c2 held DOWN이어도TouchRouterownerPC+activepointerINVALID라POINTER_DOWN은가드안거쳐remoteMOUSEDOWN가능. 해당gesture전체SWALLOW→최종UP/CANCEL, 중간shown회복돼도다음newDOWN부터·진행중layoutchange releaseonce. oldcallback→newswap→tap/clear종류·DOWN+secondfinger제한반례필요, 현재holdplace8s는swap전만/47PASS도이경계미검사.
- root전체diff/calls/시험읽음·whitespace0/HEADclean. 후보APK **8782f38701e5b6ba154a068a7d15949574a8c9a56166e248871caa4807f1769f** SHA직접확인(0c48build→779시험only APKdiff0). 현재로최종EVIDENCE/게시닫지않음, 독립unaffected가능하나c보수새고정APK로최종관련행동검사. **pickerE1 7795289 helperCommit배선SOURCEOK**, report37worker귀속. picker9af/UA2482 SOURCEOK유지.
- shimmer oracle없음정직수용하나짧은통제scroll/연속frame직접열람정성관찰통합독립에포함·수치PASS/실폰완료아님. 폰S4발열후속/기본A·B옵션결정유지. 같은writer정본c1/c2→해당diffSOURCE→새고정APK독립EVIDENCE, 새KA/PC재개방없음. root제품build/AVD새test/서명게시commitpush없음/사용자현재조치없음.

## APK32 EVIDENCE 설계 A 승인 — 2026-10-27
- 검증용 apk0232_verify.md 전문·하네스 **326e0c3**(779위ua2_desktop만/제품0)·Host prelogin auth/allowlist·fixture시작읽음. .claude/codex_apk0232_evidence_design.md 정본. **A 시험Host 역할seam+동일noseamAPK UI 채택/준비GO**. 실제 --prelogin 인증약화/디렉터리APKseam B/C실기전용대기는불요. A는로컬manual trialhostprelogin역할광고→실APKpicker/local→MediaCodec/latch증거, workerSYSTEM/Winlogon/DXGI/auth는별도실Host37/PC층근거·단일E2E주장금지.
- bit만OR하면WindowSelect잘못보내도ordinaryHostack로통과→trialHost는preloginallowlist/tripwire WindowList/Select/thumbnail금지fail·OSinput/credential/SAS/reboot/clip/file부수효과0·source는startup부터ownedpattern(--capture-window-title등), APK WindowSelect준비단계없음. macroREMOTE60_STREAM_TEST_SEAM만/기본off·출하define/bytegate+양성대조·loopback격리·APK무변경. source WGCpattern≠workerDXGI명시. 연결cookie/tag/첫IDR/최종픽셀·cancel/reselect/SH전환·금지송신0·부정대조필요.
- **SH r4 seq14202 NEEDS c1/c2 유효**, A설계GO≠8782APK출하/최종EVIDENCE. 같은writer기존정본에취합→SH보수diffSOURCE+Aseam고정→새최종APK/noseam·seam·Host해시→한독립배치→CodexEVIDENCE→기존게시. 결과미수신핑퐁불요·추상go재대기불요.
- ua2_desktop은별도IDLE/RECEIVED실UI하네스/worker9PASS귀속·actual전환미증명. 진입문서CworkSLOT Remove-Item은root밖삭제라그대로실행불가; 저장소내소유/바깥삭제없는기존조율연결방식으로정본수정·새임의이중슬롯금지. 기존유저제품설치재시작UAC조작없음. rootbuild/test/server/delete/commitpush없음/사용자현재조치없음.

## SLOT_REQUEST counterpoint·사용자 좁은 권한 질문 pending — 2026-10-27
- 검증용 remote#yj8mrmrx의기존slot사용자protocol해석이견읽음. .claude/codex_slot_request_scope_2026-10-27.md 정본. root 실제memory gpu-slot-request-file/check-other-hosts-before-e2e 전문·state10-15/handoff확인: **Rule set by 검증용10-15**, 직접사용자단일파일삭제예외원문찾지못함(메모리origin2세션user텍스트제한검색). user AGENTS:8은절대root밖삭제/필요시권한질문·자기생성파일예외없음, 단순다른산출물만대상으로해석못함.
- **functions.request_user_input_async 질문 제출/답대기**: C:/work/gnlink-wt/SLOT_REQUEST.txt 중 해당시험CreateNew생성파일1개 종료삭제만예외허용 vs 삭제없는동일파일독점FileStream핸들로변경. 첫옵션preselect≠승인. 답전바깥slot삭제만보류/기존slot보존·덮기/임의이중slot금지, SH소스/A준비독립계속. 예외승인시file1개/owninstance확인·다른TEMP/worktree/scratchX/stale타인삭제없음. 대안은파일보존·모든현runner existence→독점handle획득을검증용한번취합전환/현재run정상종료후일괄확인·Crashhandle자동회수·파일삭제없음.
- root추가탐색/정책중복질문불요, 사용자응답도착시동일정본갱신해verifierpin연결. 기존legacyprotocol을허가확인전계속삭제해도된다고승인안함. 제품/runner직접수정·build/delete/commitpush없음. 현재질문필수응답만미결/다른릴리스작업취소아님.

## SLOT_REQUEST 정정·질문 창구 일원화 — 2026-10-27
- 검증용 yj8mrmrx가 규칙은검증용10-15설정이지사용자지시가아니었다고정정, 기존Cworkslot다른실행소유가능성으로보존/삭제보류확인. 사용자현재검증용세션에지시중이라 **향후질문·답집계는검증용한창구**로일원화. root이미게시async질문은실제취소API없음·사용자이쪽중복답불요안내/추가재질문금지. 어느쪽명시답이와도같은권한상태합류·상대에게pin전달. 현재예외미승인/slot변경없음. .claude/codex_slot_request_scope_2026-10-27.md 갱신.

## SLOT_REQUEST 사용자 단일 파일 삭제 예외 확정 — 2026-10-27
- 검증용 yj8mrmrx가같은사용자AskUserQuestion원문 **그 한 파일만 삭제 허용** 전달. **해당시험이CreateNew로직접만든 C:/work/gnlink-wt/SLOT_REQUEST.txt 1인스턴스의시험종료삭제만예외허용**, 기존protocol재개/같은범위재질문불요. root밖TEMP/scratchX/worktree/남이만든·stale slot삭제는계속금지. 현재slot자기생성증거없으면건드리지않음. .claude/codex_slot_request_scope_2026-10-27.md 현재권한갱신/이전pending해소·독점핸들전환안미채택/임의이중slot없음.
- SH/A준비·최종독립검사는기존예정scope내계속(이예외가SH SOURCE c1/c2보류를해제하거나APK게시승인이되는것아님). root실제파일삭제·제품/runner수정·commitpush없음. 사용자현재추가조치없음.

## 삭제 후보 일괄 수집 사용자 지시 — 2026-10-27
- 검증용 yj8mrmrx가새사용자원문 **삭제해야될거는모아놨다가나중에내가말할때한번에/작업중간권한묻지말고** 전달. **정리삭제권한중간질문금지**, 중앙 D:/remote/remote/.claude/deletion_candidates.md에후보누적/사용자요청때검증용이일괄제시·명시승인분만삭제. 등재/후보언제든≠삭제허가·보존조건유지. SLOT시험CreateNew자기1인스턴스종료삭제예외는그대로/타인stale·TEMP·scratchX·worktree미허가. scratchX이미목록있어중복추가없음.
- agents-policy-updater 스킬읽음·적용알림, userrepo역할/기록(history.md+plancheckbox)/GitMCP/push범위가스킬일반weeklyhistory/즉시push보다우선. 문서전용정본 .claude/task_deletion_policy_2026-10-27.md 생성→검증용직접AGENT좁은상위규칙/CLAUDE구체절차/기록작성→Codex전문검토→GitMCP localcommit(별도정책taskpush없음/기존승인릴리스관련문서와연결). 제품/APK작업중단/삭제실행/새사용자승인질문없음. root slot_scope의과거미승인현재형도정정.

## 삭제 정책 문서 실행 task 연결
- 검증용 remote#yj8mrmrx 문서전용 최초task **t-94tjfghd / seq14219** 전달1. 정본 task_deletion_policy_2026-10-27.md, 다음담당검증용직접최소diff→Codex전문검토→GitMCP localcommit/기존릴리스push범위연결. 아직착수·commit완료로주장하지않음. 이작업을사용자승인질문으로되돌리지않는다.

## APK32 SH r5 + evidence A SOURCE — 2026-10-27
- 제품 **8d67710**·시험 **a6635a2**·Aseam/HEAD **ed7f4ad** clean·whole diff/calls/tests/whitespace0확인. .claude/codex_sh_strength_r5.md 정본. **NEEDS_CHANGES N1-a/b 한 묶음**, c2 gesture SOURCEOK/A시험Host구성GO. N1-a SharpenOutputLog seq가인스턴스0→renderer A/B첫token모두BASE+1, oldcallback을currentlog의newkind/gen/decoder로오판가능. 기존unknown test의otherlog비어있어미검출. 전역수명유일token/renderer·surface상관·양쪽채운충돌반례필요. N1-b Main newFrame=latched.frame&&classifier라REDRAW가F첫실제표시면first-output/visible확인못함→static대기/fallback. VIDEO/REDRAW 원frame첫visible1회/새decode0·clearunknown제외·gate연결반례필요. clear/unknown currentlatch에oldshownproof남용도함께검사.
- 같은rendereroutput기록beforeSwap/실패drop·bounded64·sameLayout실redraw방향수용. c2 RouterDOWN→wholeSWALLOW/secondpointer/localpan/geometryinterruptonce 제품연결확인. pure275/mut3/AVD는worker귀속/경합·multitouchAVD아님. root APK **1d4a05f4f318496a895ac748be5705365598c3de677c6f4652988034bd3deebd** SHA직접확인, 8782구후보폐기.
- A env시험macro only/defaultoff·=1mask/allowlist FORBIDDEN종료/credentialSASreboot방지·=2대조/ordinarynegative검토수용. 하네스loopbackstartupownedpattern/inputnone 유지/실worker auth/SYSTEM/DXGI증거와분리. 정확Host881b..shipStream1c..gate54는보고귀속·최종definebyte증거유지.
- 검증용독립AVD현재시작중·중단요구없음/현hash결과부분보존가능, 출력수명·first-output관련최종행동은보수새고정APK에서확인/현재PASS자동승계금지. 같은writerN1-a/b→diffSOURCE→최종독립EVIDENCE, picker/UA2/c2재개방없음. 삭제후보중간질문금지·slot자기생성예외유지·문서task t-94별도. root제품buildtest서명게시commitpush없음/사용자현재조치없음.

## 삭제 운영 정책 문서 r1 전문 검토 OK — 2026-10-27
- task **t-94tjfghd**, main5970f04위4tracked파일+26/-1 고정diff전문/실제파일·범위·check0·4파일CRLF/loneLF0 root확인. .claude/codex_deletion_policy_r1.md 정본/seq14224 verifierpin **OK→해당4파일GitMCP ConventionalCommit/단독push없음**. root밖삭제금지·작업중질문금지·후보보존/요청시일괄명시승인·소유/조건/보존/slot1예외/타인stale제외·history출처정정·plancheckbox만 모두수용.
- 미세메타만재왕복없이허용: AGENT유일한예외→유일한상시예외, history CodexOK·checkCRLF완료표기. 다음담당검증용commit해시/최종범위/clean회신→root최종확인. 아직commit완료주장아님/삭제·제품·추가push·사용자질문없음. APK진행별개·스킬일반weekly/push보다userrepo규칙우선.

## 삭제 정책 문서 local commit 완료 확인 — 2026-10-27
- main **2137f3a3a261422b548236398ae61e4dcd011e0b**(parent5970f04),4파일26+/1-·Conventional docs(policy). root실commit전체diff·승인된AGENT상시예외/historyOK메타만최종추가·check0·trackedclean 확인, origin/main5970f04라단독push없음. .claude/codex_deletion_policy_r1.md 최종갱신. t-94tjfghd의구seq14220 turn-ended가늦게도착했으나이미14224OK→commit완료라추가commit불요(14226참조회신뒤gitread로발견/최종확인통지). 사용자최종안내연결·실삭제/제품없음·APK독립배치별개.

## APK32 SH r6 SOURCE 완료 — 2026-10-27
- 제품 **d29a034**·시험 **b554e76** 전체diff/실Activityqualifier/classifier/first-output연결읽음/check0·HEADb554clean. .claude/codex_sh_strength_r6.md 정본 **SOURCE OK r5N1-a/b닫음**. NEXT companionAtomicLong 프로세스전역token·양쪽log교차/동시8000대조, Video/Redraw picture동등→원decoderFrame/surface classifier첫표시1회·실Activity지연/visible/gate같은qualification사용. CLEARunknown NONE_SHOWN=-1을shown에반영후frame처리전return→이전proof삭제. direct/Handoff원timestamp·boundedlog/beforeSwap/실패withdraw유지. 기존c2/picker/UA2/A seam승인유지.
- 단위277/변이3은worker귀속·root저장gradleShr6 A/B/C·코드읽음/직접buildAVD없음. report정본현재r5까지라r6주장은A2A+로그기준으로명시/새후보report에갱신허용·SOURCE추가왕복불요.
- verifier현재1d4APK독립배치끝난뒤worker1새noseam/shseam r6포함build→새hash/버전ABI signer/gate고정→관련출력token/first-output/static/renderer교체A/Boff/clearfallback/입력·A UI/UA2 최종후보독립EVIDENCE→Codex→기존게시. 이전해시PASS자동승계없음/불필요순수전량반복없음·새go/삭제권한질문없음. root제품buildtestdelete서명게시commitpush없음/사용자조치없음.

## 운영
- repo `D:/remote/remote`, PowerShell. AGENTS.md/CLAUDE.md와 최신 task를 읽는다. rg 검색, UTF8 입출력. 다른 branch/사용자 변경을 섞지 않는다.
- Codex는 제품 직접 build/편집 대신 읽기 검수와 `.claude/` 판정서를 기본으로 한다. 판정서만이면 새 commit을 만들지 않고, 구현 history/계획/commit은 같은 기능 task에 취합한다.
- 대응한 inbox 항목은 resolve로 닫는다. 실행 가능한 자기 일이 남았는데 ‘지시 대기’로 멈추지 않는다. 외부 결과 대기 시 기존 task/다음 담당/기다리는 결과를 명시한다.

이 파일은 clear용 미추적 운영 메모다. 제품/공개 문서 변경·commit/push 없음.
