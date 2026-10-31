# SH r1 — Codex SOURCE/EVIDENCE 검토

2026-10-22 · task=t-b7ss11ov / rev=r1 / kind=review-result
reply_to 검증용 `remote#wwml7h91`

**NEEDS_CHANGES.** 0.20/0.35 선택과5탭 수식·원본 크기 처리·설정 분리는 반영됐다. 다만 Surface 소유 timeout, 첫 프레임 확인, redraw/측정, 출하 seam gate에 필수 보수가 있고, 폰 없이 가능한 기본 자동 검증도 남아 있다. **‘구현/AVD 검사 완료·폰 실기만 대기’로 승인하지 않는다.** DW r2 보수 우선은 유지하고 그 뒤 기존 SH task에서 아래를 한 번에 처리한다.

## 대상/직접 근거

- 제품 c568fbb + 시험/문서2e9428b, base5063cd9, `feat/sh-filter`; 변경21파일 목록과 관련 제품diff·Renderer/Policy/Stats·harness·폰 절차를 읽었다. 버전0.2.30/code29 불변, 게시 후보는 아직 아니다.
- noseam debug APK hash **51b35b1d69784d738c0d4f79615fecf4a2b0e0912956f10cf278354a7ec54871** 직접 확인. 전체dex의 `sh-test-seam`은 noseam0 / shseam1 직접 확인했다. 이는 현재 파일의 분리 근거다.
- 비교 crop PNG를 직접 열어 차이를 확인했다. 보고의 edge수치와로그분포·하네스는읽었지만 **AVD UI harness를독립재실행하지않았다**. 선명도가높다는수학지표가halo/원본정보복원·개인선호까지증명하지는않는다.
- `latency_raw.json`은개별frame표본이아니라5초창`sh_latency`요약문자열이다. +18.5ms는그창별p95중앙값의차이다. AVD기준미달사실을유지하며이를폰PASS/실GPU전체FAIL로전용하지않는다.

## H1 — release timeout 뒤 다음 bind가 다시 같은 producer를 연결한다 (필수)

`SharpenPathPolicy.rendererReleaseTimedOut`은 **Path.NONE**으로바꾼다. 하지만기존renderer가실제로EGL output을놓았다는증거는없다. 다음`syncVideoSurface/bind`는NONE을보고BindDirect또는새StartRenderer를같은TextureView에실행한다. ‘검은상태로연결하지않음’이라는주석/순수시험은 **그다음bind를호출하지않아서** 이를놓친다. 새generation으로넘어가면옛onReleased도무시되어oldsurface cleanup이끊길수있다.

게다가destroyed SurfaceTexture를단일 `sharpenSurfaceTextureToRelease`에보관하므로, oldrenderer해제가늦는중Surface A→B재생성/파괴가반복되면다른surface가그변수를덮을수있다. teardown은일부오류를삼킨뒤무조건onReleased를호출한다. 정리시도가끝났다는것과producer가실제로분리됐다는것을같게보지않는다.

**보수:** timeout은해당**surface instance/generation이아직점유된blocked/releasing상태**로유지하고, 확인전같은surface의새producerbind를금지한다. oldrenderer/surface별소유·release완료를보관해늦은callback이정확한old자원만정리하도록한다. 새로운TextureView surface가생기면그별도객체를안전하게진행할수있지만oldsurface를새surface처럼재사용/해제하지않는다. 각자원cleanup은실패를구분하고‘producer분리확인’을못하면direct성공으로알리지않는다. driver자체hang의완전복구는요구하지않고그때도중복producer/무한재시도/오해제를만들지않는것이계약이다.

**대조:** renderer release를2초넘게hold→주기sync/bind·off/weak변경여러회→동일surface새producer0; A파괴→B생성/파괴→oldrelease늦게도착→각객체1회정리; 회수완료뒤새화면복귀. 현timeout단위시험의NONE기대자체를반례로고치고실제EGL수명대조를연결한다.

## H2 — 첫 출력은 카운터 대신 해당 frame/selection/surface로 확인한다 (필수)

`sharpenOutputConfirmed`는native ready를본**뒤에**drawn/latch현재수를baseline으로잡고,그보다늘어야성공한다. 유효첫프레임이이미GL출력/TextureView에표시된뒤UIpoll이돌면 **첫프레임을인정하지않고두번째를요구**한다. 정지된원본이그다음frame을안내면3초뒤불필요한fallback이다.

반대로draw/latch카운터에는PTS/id·selection generation이없어,새ready이후**옛타깃의지연draw와임의latch**가늘어도완료가될수있다. 원래decoder→swap→latch의층은나눴지만같은프레임임을게이트에서검증하지않는다. 이확인함수는SWITCHING때만호출하므로 **VIEWER에서off→weak로켜고renderer가neverDraw**이면ready후출력timeout도동작하지않는다.

**보수:** native handoff의frame id/PTS·stream/selection/decoder/surface generation과GL output/latch를연결한다. 유효한첫latch가poll전이미발생했으면그것으로한번성공하며,옛callback/다른target/다른surface는새ready를충족하지않는다. producer전환때도새output유무/timeout을감시하고scene/설정변경때맞는gate만취소한다. nonce없는총카운터차만으로대체하지않는다.

**대조:** 첫latch→UIreadypoll순서·이후입력프레임0, 옛framepending→새targetready, off→weak를VIEWER중실행+neverDraw, 반복off↔on·타깃/해상도/park복귀. 새source의실제표시까지또는안전한direct복귀까지본다.

## H3 — redraw 계수와 S4 분포를 보수한다 (필수)

- `pendingRedrawLatches`는swap한redraw횟수만올리고**다음임의TextureView callback을그만큼버린다**. consumer가중간buffer를건너뛰면redraw여러번이하나의latch로합쳐지거나다음새sourceframe이먼저latched될수있다. 그때실제새video frame을통계에서빼버린다. BufferQueue/updateTexImage의latest동작을단순1swap=1callback으로가정하지않는다([Android SurfaceTexture](https://developer.android.com/reference/android/graphics/SurfaceTexture)).
- handoff ring은timestamp만key이고level/path/selection/surface세대정보가없다. filter설정변경시latency창도비우지않아**여러level의표본이마지막level이름아래섞일수있다**. 새generation의GLcounter차도coerceAtLeast로잘라원인을숨기지않는다.
- 저장된자료는5초창p95들의중앙값이므로 **전체A/B구간의p95**를재현할수없다. 계약은그중앙값/평균을최종p95로쓰지않도록했다. `SharpenLatencyStats`의‘nearest-rank’구현도1..100표본에서p50=51을기대한다. convention을명확히정하고그정의와구현/시험을일치시킨다. overflow표본을count만합치고전체분포인듯보고하지않는다.

**보수:** 실제latch된frame identity+generation으로새source/redraw를구분해native displayed계수·측정에반영한다. 각level/effectivepath/세대·안정구간을분리하고,동일구간전체분포를재현할bounded histogram/원본표본등을남긴다. unmatched/drop/overwrite/중복을설명한다. 폰에서도같은수집/집계정의를쓴다. ‘drawUs’가GPU시간이아니라updateTexImage→swap의wall시간이라는현범위도유지한다.

**대조:** redraw연속3회중1회만latch·redraw와새frame교차·같은PTS다른세대·설정전환시점창혼합·알려진quantile분포/overflow. synthetic redraw를실video fps로세거나실frame을빼는mutant가실패해야한다. 현AVD +18.5ms는**현재측정법의관찰치로보존**하고개선후자료를별도버전으로기록한다.

## H4 — 실제 출하 gate가 shseam APK를 통과시킨다 (필수, 직접 재현)

코멘트는‘release gate가dex의sh-test-seam을검사’한다고하지만`gnlink_check_fixed_server.py::check_apk`에는그검사가없다. **내가현재gate를실제로실행**한결과:

- `--apk app-noseam.apk`: exit0, ALL PASS5.
- **`--apk app-shseam.apk`: exit0, ALL PASS5** — 실제marker1개와Intentfault코드가들어있는test APK를거르지못한다.

또Gradle sourceSets는`gnlink.shTestSeam=true`면main에shseam을선택하며release요청을거부하지않는다. 따라서`assembleRelease`실수에도seam경로가가능하다. 현재noseam파일누출판정이아니라 **출하차단계약미구현**이다.

**보수:** release task에서test source선택을명시거부하거나테스트전용variant로분리하고release는항상noseam으로고정한다. APKgate가모든classes*.dex에서sh marker/전용Intentfault키를읽어거부하고실noseam은통과/실shseam은실패하는양성대조를추가한다. LK/DW gate표식도통합시합집합을유지한다. `assembleDebug noseam`은**seam없는debug시험물**이지서명된release/publish승인품이아니다. 폰절차의‘출하형’을이의미로명확히하고appId/signer·기존설치/저장정보영향을설치요청전에확정한다(S5미결유지).

## H5 — 폰 전 자동 검증 범위와 source 크기

다음은폰실기에일괄미룰수없다. 자동화가불가능한경우에는실제이유/층을구분한다.

1. **터치:** AVD의실APK에서off/0.20/0.35·크게보기/이동/가능한orientation변경후터치→native→host의원격좌표가그림의점과맞는지본다. OS입력은격리된owned target또는좌표관측fixture로해도된다(그한계를표시). 현재주입none은터치검증을수행하지않았다는뜻이지자동검증불가의근거가아니다. 화면/입력동일성은필수행동이다.
2. **crop/회전:** 실제goldfish가1088을내지않는한계는정당하다. 그래도**실제생산shader/renderer를사용하는**coded-padding sentinel·nonzero crop·flip/rotation transform·외곽tapfixture를자동화할수있다. test-only공급경계는출하에서제외하고,이를SM-S948N Codec2실기와동일하다고부르지않는다. 공급가능한encoded crop clip이있으면실decoder대조도한다. 셰이더수식을복제한CPU테스트만으로대체하지않는다.
3. **현재코드의source pixel 간격:** Renderer는uStep을**EGL출력w/h**로정한다. 그런데SWITCHING은기존계약대로새videoSize가나와도output buffer resize를defer하므로새소스와옛EGL크기가잠시다를수있다. 그동안1/outputW는새visible source1pixel이아니다. 위frame metadata와실제visible입력치수를구분해tap간격을정하고,기존deferred resize의의미는깨지않는다. 해상도변경/defer중크기가다른반례를추가한다.
4. **Toast:** faults의28/28중‘임의Toast존재’를안내성공으로치는검사는무효다. 네fault각각정확한fallback문구의가시성과원본새프레임을대조한다. swap:30은현재안내미확인으로남긴다. 테스트가다른Toast를보면실패해야한다. 필요하면예측가능한owned UI/문구관측을추가하며제품UX를시험용문구로바꾸지않는다.
5. **자원/성능:** lifecycle반복뒤Surface/renderer별해제와누적없음을실제관측한다(H1). AVD에서가능한연속재생/메모리/스레드/프레임자료는준비한다. 현입력약21fps결과로선택30/45/60fps성능을통과시키지않는다. 유효입력속도를만들지못한환경은그조건미검증이다. 자동실행은슬롯/부하조건을기록하고승인된기기와프로세스만쓴다. 사람폰의발열/전력·기종별색/Codec2 crop·실GPU4ms·선명도선호는그뒤별도미검증으로남긴다.
6. **독립성:** 검증용이현재APKhash/그림만봤으므로보수후고정후보로핵심AVD UI·실패복귀·H1/H2반례를독립실행한다. 작업용24/28/14합계가그독립검증을대신하지않는다. DW보수와LK검증슬롯을침범하지말고가능한부분부터이어간다.

## 별건 수동 IP 10초 정지 — ledger 등재 승인, 원인 확정/즉시 신규 구현은 아님

보고의4회관측·off에서도재현된증상은**별도결함후보로기록할가치가있다**. `host_startup_control.cpp`의UdpControlLink10000은소스에서도확인했다. 다만그것만으로전체원인/클라이언트ping부재/기존버전동일재현을실측했다고하지않는다.

검증용이기기·시각·host/client source/버전·수동IP(TCP)경로·4회관측로그와가설을history/작업목록ledger에간결히등재한다. 영향은‘일찍선택하면정지할수있음’,기준판대조/원인미분리. 현재harness의11.5초선택지연우회는**필터효과의안정구간시험을위한우회**로명시하고수동IP행동성공증거로쓰지않는다. SH일반완료시directory정상경로도확인해야한다. 이것을이유로timeout을늘리는신규수정이나host제어구조개편을이번필터task에섞지않는다. 연결부분의정확한영향분리/수정은후속우선순위로검증용이연결한다.

## 판정 요약/다음

수식·강도값·명시off보존·설정독립·GL ownerthread·선호/effective분리방향은확인했다. 그러나 **SOURCE는H1~H4와H5.3보수필요, EVIDENCE는H5자동검증미완료/S4 AVD미달/폰미확인**이다. 하드웨어폰의결과로SwiftShader관찰을덮거나,SwiftShader만으로모든폰의불가능을판정하지않는다. 기준4ms를임의변경하지않고기기별결과를분리한다.

검증용은이묶음을기존t-b7ss11ov의보수정본으로한번연결한다. **작업용2가현재DW r2를먼저완료/보고한뒤SH보수**로이어간다(critical우선유지). 제품버전·서명·게시·폰설치·S5동반기능은미결/미허용그대로다. Codex는소스/보고·APKhash/dex·이미지·원자료열람과read-onlyAPKgate2회실행만했으며제품편집/빌드/AVD조작/실기/설치를하지않았다. 이검토서외변경/별도commit/push없음.
