@echo off
rem encoder-rc-budget: the Home PC isolated run, one folder, no install.
rem
rem Put beside this file (the verifier assembles them from a fixed build and records each hash):
rem   remote60_fec_single_chunk_host_e2e_test.exe   the harness (it stages a copy of each host per run)
rem   GNLinkStream.candidate.exe                     the candidate host (the commit under test)
rem   GNLinkStream.baseline.exe                      the baseline host (the version the field runs)
rem Nothing is installed, no service is touched and the user's own GNLinkStream keeps running: every
rem host this starts is isolated (own LOCALAPPDATA under the staging folder, input injection off,
rem captures only the harness's own window). Results land in .\results\<timestamp>\ and NOTHING
rem else: the harness is copied into that folder before it runs, so its test-scratch (host.log and the
rem event_*.pgm frames per run) is created there and belongs to this run alone.
rem
rem   run.cmd         transitions + matrix: the control smoke plus the decode COMPARISON material
rem                   (baseline beside candidate; ~25 min)
rem   run.cmd quick   transitions only: the CONTROL smoke (R/E/plan/coalescing paths; a few minutes).
rem                   It is not a quality comparison and must not be reported as one.
rem   run.cmd diag    r8: the SHORT DIAGNOSTIC SET (~4 min) for the Home PC main-loop hangs and the
rem                   motion regression -- five phases, each separating two hypotheses (see below).
rem   run.cmd home5   r11: H0 (the log-write discrimination set, baseline only, ~2 min) FIRST, then the
rem                   home4 phases. H0 answers "does the host stop when its stdout goes to a local file,
rem                   to the network share, to NUL, with or without the harness reading the file?"
rem   run.cmd home4   r9: the ONE Home PC run after the r9 fixes (~6 min): the ORIGINAL candidate's
rem                   stall evidence first (dumps), then the r9 candidate on the same cells, then a
rem                   CBR 51 contrast row. Needs GNLinkStream.orig.exe beside the three files (the
rem                   a510cfc build the third run used); without it the original phases are skipped
rem                   and say so.
rem Exit code: 0 only when every phase that ran returned 0. Each phase's own exit code is in
rem results\<timestamp>\<phase>.exit; 1 there means FAIL lines in that phase's .log; 3 means the
rem harness could not get a capture item for its window even with the visible fallback (see below) --
rem then the matrix is NOT run (matrix.exit says skipped) and this script returns 1.
rem
rem NETWORK DRIVE. If this folder is on a network drive (the Home PC ran from Z:), the whole run is
rem staged to a LOCAL folder (%LOCALAPPDATA%\Temp\rc_home_bundle\<timestamp>) and run from there, and the
rem results are copied back here at the end. The hosts write host.log synchronously; a stall of a
rem network file system would look exactly like a main-loop hang, so the local copy removes that
rem variable (not a verdict on it -- the r8 report compares both).
rem
rem THE WINDOW THE HOST CAPTURES. The harness paints its test page into its own window and the host
rem captures THAT window, never the screen. By default the window is invisible (alpha 1/255,
rem click-through, tool window). On the Home PC (Windows 10 22H2, RTX 3070 Ti) WGC refused to create a
rem capture item for that window in every run ("capture item create failed" in host.log, the host
rem exits 8) and refused the first fallback (a caption-less topmost popup) too -- both are accepted on
rem the Windows 11 build machine. Before starting any host the harness now asks WGC itself for each
rem window mode and prints the answer: "wgc-probe: mode=invisible|visible-plain hr=0x........". A
rem refused mode is skipped (no host is started for it). The ladder is invisible -> visible-plain:
rem   visible-plain = an ORDINARY WINDOW with a caption and a taskbar entry, 1920x1080 page inside,
rem   centred on screen, shown without taking focus. WHILE THE HARNESS RUNS YOU WILL SEE ONE EXTRA
rem   WINDOW ON THE TASKBAR titled "remote60 fec single-chunk target ...". Leave it alone: do not
rem   close, move, minimise or cover it. It shows only the test page (text/scroll/video patterns).
rem It is never your desktop that gets captured or saved -- a host that cannot find the harness window
rem and offers its monitor fallback is terminated by the harness and that run's frames are deleted.
rem If WGC refuses both modes the phase ends at once with exit 3 and both HRESULTs in the log.
rem Every phase line carries window-mode=invisible or window-mode=visible-plain; each run's host-exit:
rem line quotes the probe HRESULT and the host's last log line (its exit reason) so a dead host is
rem named, not waited out.
rem
rem What one run collects:
rem   transitions.log  6M->1.5M->6M (R at once from the host's own apply marker, E per instance, the
rem                    same target twice), fps 30->60, and the E-ONLY sequence at a fixed 1080p
rem                    geometry (5M, 6M, 5M, 5.5M, 5.2M, 5.4M, 5.4M on a ms schedule placed inside and
rem                    outside the host's 2 s coalescing interval: adopt / defer / cancel / latest wins /
rem                    repeat no-op; the EONLY lines print where each tune and apply fell).
rem   matrix.log       5 scenes x 30/60 fps x 1.5/3/6 Mbps, baseline beside candidate: wire windows
rem                    (1 s / 250 ms / 267 ms) against R, the host's R/E and plan readback state per
rem                    instance, realtime decode gaps, event-matched change->picture latency, matched
rem                    PSNR (whole picture + text rows), IDR sizes, payload actually sent. Each RCMATRIX
rem                    row carries valid=0|1 hostExit=alive|<code> stallMaxMs= paints=: a row with
rem                    valid=0 (dead host, no frame, capture stalled >2 s) is a FAILED run, not a rate.
rem   test-scratch\    per run: host.log and event_*.pgm (the first decoded frame of each change event,
rem                    visible Y plane) -- the actual text the viewer got, for a human to read.
rem                    hang-pid<N>.dmp: a minidump of the HOST (thread stacks) taken by the harness when a
rem                    live host delivered no frame for 5 s -- the stuck stack of a "main-loop hang". Open
rem                    with the PDB of the exe whose hash identity.txt records (WinDbg: ~*k).
rem   identity.txt     hashes of the three exes (hash lines only), OS version/build (CIM), GPU + driver
rem                    (CIM), session list, the REMOTE60_* variable NAMES still set, folder + a
rem                    network-drive note. One encoding: UTF-8 (chcp 65001), ASCII in practice.
rem
rem What counts as the NVIDIA policy having run (per INSTANCE, in each run's host.log):
rem   "rc-identity ... vendorId=VEN_10DE profile=nvidia_pre_types"
rem   "rc-plan stage=pre-types ... maxQp=51"           the plan went in before the media types (the r9
rem                                                    candidate; the ORIGINAL a510cfc host says maxQp=44)
rem   "rc-plan stage=post-types ... setAccepted=1 state=accepted effective=1"
rem                                                    every Set accepted and the readback agrees
rem   and the ENCODER OUTPUT actually moved with E: the stats lines' appliedEncoder changes across the
rem   E-only run while "size=1920x1080" never does, and the sent payload follows it. appliedEncoder and
rem   sentPayload are the plan value and the post-gate wire, NOT the AU output itself -- the AU effect
rem   is read from the per-run [keyframe]/stats AU sizes and the event_*.pgm frames.
rem In the E-only run: "encoder-budget ... action=deferred" then "deferred retarget applied" once.
rem In every matrix row: rcWrites=0 -- this counts reconfigure AND governor writes (reconf=0 gov=0; the
rem ceiling is static on this profile; a governor write there would be a real finding). The E-only
rem line likewise: rcReconfigureWrites=0 rcGovernorWrites=0.
rem win267 is the EXTERNAL wire over a 267 ms window (the receive side's clock), not the probe AU.
rem A run whose profile is not nvidia_pre_types, or whose plan reads back mismatch/unknown, is not
rem evidence for the NVIDIA policy -- report it as what it is.
rem Not covered here and still unverified (section 5 of the report): FrameGate behaviour, real
rem recovery after a stall, DesktopText at 1080p. Note the 1.5M and 3M window cells run at 720p (the
rem ladder steps down below 6M); only the 6M cells are 1080p.
rem
rem THE DIAGNOSTIC SET (run.cmd diag). Each phase separates two hypotheses; each has its own .exit and
rem .log; a hang in one phase does not stop the next (they are independent) -- only a refused capture
rem item in D1 stops everything.
rem   D1 control    textscroll 30 fps 6M 1080p, candidate, 10 s. Capture works at all? (no scaler)
rem   D2 scaler     textscroll 30 fps 1.5M, 1920x1080 page -> the ladder encodes 1280x720 through the GPU
rem                 scaler, candidate then baseline, 10 s each. Home: hung before the first frame in 24/24
rem                 such cells on BOTH hosts. (With a baseline the 1.5M decode-gap gate is relative, as in
rem                 the matrix; a candidate-only 1.5M run trips the absolute gap ceiling on any host.)
rem   D3 no-scaler  the same cell with a 1280x720 page (both hosts): 720p encode WITHOUT the scaler. D2 hang + D3 ok
rem                 = the scale path; D2 and D3 both hang = the low rate / 720p encode itself.
rem   D4 motion     motion 60 fps 6M 1080p, CANDIDATE FIRST then baseline, 12 s each: the regression
rem                 candidate, with the fixture paint counter and the capture-stall counter per run.
rem   D5 transitions the three transition runs (16 s): tune/deferred/key path, then the hang if it comes.
rem Every hung host leaves hang-pid<N>.dmp (thread stacks) in its run folder.
rem
rem THE H0 SET (run.cmd home5 runs it first). In the fourth Home run EVERY host -- baseline, original, r9 --
rem stopped logging 1-2 s after start with its main thread inside the stats line's write (ntdll under
rem std::cout), while the third run (hosts logging to Z:) had healthy 6M cells. Five 10 s baseline-only runs
rem of the same cell (textscroll 30 fps 6M 1080p) separate the suspects; each prints host-log: lines/s and
rem its own exit, and a hang leaves a dump as usual:
rem   H0a  host stdout -> NUL                       (no file write at all)
rem   H0b  host.log in the LOCAL staging dir          (the fourth run's setting)
rem   H0c  host.log on Z: (this folder)              (the third run's setting; only when Z: is a network drive)
rem   H0d  local host.log, harness never reads it      (--no-log-poll: the 200 ms monitor-fallback scan off)
rem   H0e  local host.log, no dump trigger at all      (--no-dump)
rem Reading: a hang in b/d/e but not in a = the file write itself; in b but not d = the harness's reads;
rem in b but not c = local disk/AV vs share; everywhere incl. a = not the log at all (D3D/driver).
rem
rem THE HOME4 SET (run.cmd home4). Phases H1-H2 run the ORIGINAL candidate so the cause of the third run's
rem stalls is recorded and is not hidden by the r9 fixes; H3-H6 run the r9 candidate; H7 is the contrast
rem row; H8 is the text-picture pair. Two originals, named apart (identity.txt has both hashes):
rem   GNLinkStream.orig-b27.exe   the EXACT bytes the third run used (SHA256 b27c0b90..., 1,428,992 B; no PDB)
rem   GNLinkStream.orig-sym.exe   the a510cfc source rebuilt with a PDB (a different link; its dumps read back
rem                               with names; it is NOT byte-identical to b27 and does not stand in for it)
rem H1 uses b27 (the stall itself, as it happened); H2 uses orig-sym (the stall with a readable stack).
rem H1/H2 are DIAGNOSTIC: an invalid original row is "DIAGNOSTIC-COLLECTED" (phase exit 4), never a PASS and
rem never a product failure; a missing original makes the set INCOMPLETE (this script returns 1). Every
rem host that hangs or starves leaves hang-pid<N>.dmp; read them with the PDB that matches THAT exe:
rem   remote60_dump_stacks.exe <dmp> <folder with that exe and its .pdb>
rem   H1 orig-720p   textscroll 30 fps 1.5M (1080p page -> 720p through the GPU scaler), orig-b27 then baseline
rem   H2 orig-motion motion 60 fps 6M 1080p, orig-sym then baseline (the capture-publication stall at the
rem                  original's QP 44; this host has a PDB, so its dump reads back with names)
rem   H3 r9-motion   motion 60 fps 6M, r9 candidate then baseline  (positive: candidate fps >= baseline)
rem   H4 r9-text     textscroll 60 fps 6M, r9 candidate then baseline
rem   H5 r9-720p     textscroll 30 fps 1.5M through the scaler, r9 candidate then baseline (bounded Map)
rem   H6 r9-trans    the three transition runs (down/up, fps, E-only), r9 candidate
rem   H7 cbr51       motion 60 fps 6M with REMOTE60_NATIVE_RC_MODE_NUM=0 (CBR, same E/GOP), r9 candidate
rem                  only -- a contrast row for the mode question, not a policy (its exit does not decide the set)
rem   H8 text-pgm    singlechange 30 fps 6M 1080p, r9 candidate then baseline, --save-frames: the marker
rem                  scene writes event_<id>_1920x1080.pgm (the first decoded frame of each change) for BOTH
rem                  hosts, so a person can read the text at QP 51 beside the baseline's. (--save-frames only
rem                  writes for marker scenes; motion/textscroll have no events -- r10 correction.)
rem The r9 host is the candidate of record; the originals and r9 are different binaries.

setlocal
chcp 65001 >nul
set "HERE=%~dp0"
set "STAMP=%date:~0,4%%date:~5,2%%date:~8,2%_%time:~0,2%%time:~3,2%%time:~6,2%"
set "STAMP=%STAMP: =0%"
set "MODE=%~1"

rem Local staging when this folder is on a network drive (see NETWORK DRIVE above).
set "NETDRIVE=0"
net use %HERE:~0,2% >nul 2>&1 && set "NETDRIVE=1"
if "%HERE:~0,2%"=="\\" set "NETDRIVE=1"
set "WORK=%HERE%"
if "%NETDRIVE%"=="1" (
  set "WORK=%LOCALAPPDATA%\Temp\rc_home_bundle\%STAMP%\"
  mkdir "%LOCALAPPDATA%\Temp\rc_home_bundle\%STAMP%" 2>nul
  echo network drive %HERE:~0,2% detected: running from local staging %LOCALAPPDATA%\Temp\rc_home_bundle\%STAMP%\ and copying the results back here at the end
  copy /y "%HERE%remote60_fec_single_chunk_host_e2e_test.exe" "%LOCALAPPDATA%\Temp\rc_home_bundle\%STAMP%\" >nul
  copy /y "%HERE%GNLinkStream.candidate.exe" "%LOCALAPPDATA%\Temp\rc_home_bundle\%STAMP%\" >nul
  copy /y "%HERE%GNLinkStream.baseline.exe" "%LOCALAPPDATA%\Temp\rc_home_bundle\%STAMP%\" >nul
)
set "OUT=%WORK%results\%STAMP%"
mkdir "%OUT%" 2>nul
if not exist "%OUT%" echo cannot create %OUT% & exit /b 2

rem Nothing inherited may change what the host does: clear EVERY REMOTE60_NATIVE_* override a shell
rem might carry (rate control, governor, async poll, decoder backend, static-scene fps, keyint, ...),
rem so the run measures the product's defaults. The namespace is cleared as a whole; the NAMES that
rem remain under REMOTE60_* afterwards are recorded in identity.txt -- names only: a shell may carry
rem REMOTE60_DIR_ADMIN_KEY / REMOTE60_SIGNUP_KEY and the like, and no value of any variable may end
rem up in the folder that is handed back. The harness's own test-support switches (REMOTE60_E2E_*) are
rem cleared too: a Home run measures the real window, not a forced failure or a forced dump.
for /f "tokens=1 delims==" %%v in ('set REMOTE60_NATIVE_ 2^>nul') do set "%%v="
for /f "tokens=1 delims==" %%v in ('set REMOTE60_E2E_ 2^>nul') do set "%%v="
rem The harness starts real hosts: this is the explicit opt-in it requires.
set REMOTE60_ALLOW_HOST_E2E=1
rem Standalone: the harness's scratch root is normally pinned at build time inside the build machine's
rem repository and refused anywhere else. Here the folder the harness runs from IS the root (the copy
rem under results\<timestamp>\): test-scratch is created beside that copy (same guards: must be
rem created by this run, no links in the way). Test-support switch only; the product binaries do not
rem read it.
set REMOTE60_TEST_SCRATCH_STANDALONE=1

set "E2E_SRC=%WORK%remote60_fec_single_chunk_host_e2e_test.exe"
set "CAND=%WORK%GNLinkStream.candidate.exe"
set "BASE=%WORK%GNLinkStream.baseline.exe"
set "ORIGB27=%WORK%GNLinkStream.orig-b27.exe"
set "ORIGSYM=%WORK%GNLinkStream.orig-sym.exe"
if "%NETDRIVE%"=="1" if exist "%HERE%GNLinkStream.orig-b27.exe" copy /y "%HERE%GNLinkStream.orig-b27.exe" "%WORK%" >nul
if "%NETDRIVE%"=="1" if exist "%HERE%GNLinkStream.orig-sym.exe" copy /y "%HERE%GNLinkStream.orig-sym.exe" "%WORK%" >nul
if not exist "%E2E_SRC%" echo missing %E2E_SRC% & exit /b 2
if not exist "%CAND%" echo missing %CAND% & exit /b 2
if not exist "%BASE%" echo missing %BASE% & exit /b 2
set "E2E=%OUT%\remote60_fec_single_chunk_host_e2e_test.exe"
copy /y "%E2E_SRC%" "%E2E%" >nul
if not exist "%E2E%" echo cannot stage the harness into %OUT% & exit /b 2

set "ID=%OUT%\identity.txt"
>"%ID%" echo == identity ==
rem certutil's wording is localized (and comes out in the OEM code page whatever chcp says): keep only
rem the hex hash line of each (findstr: a line of hex digits only), with the file named by this script.
>>"%ID%" echo harness=%E2E_SRC%
certutil -hashfile "%E2E_SRC%" SHA256 | findstr /r /i /c:"^[0-9a-f]*$" >>"%ID%"
>>"%ID%" echo candidate=%CAND%
certutil -hashfile "%CAND%" SHA256 | findstr /r /i /c:"^[0-9a-f]*$" >>"%ID%"
>>"%ID%" echo baseline=%BASE%
certutil -hashfile "%BASE%" SHA256 | findstr /r /i /c:"^[0-9a-f]*$" >>"%ID%"
if exist "%ORIGB27%" (
  >>"%ID%" echo orig-b27=%ORIGB27%
  certutil -hashfile "%ORIGB27%" SHA256 | findstr /r /i /c:"^[0-9a-f]*$" >>"%ID%"
) else (
  >>"%ID%" echo orig-b27=absent
)
if exist "%ORIGSYM%" (
  >>"%ID%" echo orig-sym=%ORIGSYM%
  certutil -hashfile "%ORIGSYM%" SHA256 | findstr /r /i /c:"^[0-9a-f]*$" >>"%ID%"
) else (
  >>"%ID%" echo orig-sym=absent
)
rem r9 (6): the PDB identity (RSDS GUID/Age) of each host exe, so a hang dump's module can be matched to the
rem PDB kept with that build (the PDBs themselves travel with the verifier's bundle, not in the results).
>>"%ID%" echo == pdb identity (RSDS GUID/Age per exe) ==
>>"%ID%" 2>&1 "%E2E%" --pe-id "%CAND%"
>>"%ID%" 2>&1 "%E2E%" --pe-id "%BASE%"
if exist "%ORIGB27%" >>"%ID%" 2>&1 "%E2E%" --pe-id "%ORIGB27%"
if exist "%ORIGSYM%" >>"%ID%" 2>&1 "%E2E%" --pe-id "%ORIGSYM%"
>>"%ID%" 2>&1 "%E2E%" --pe-id "%E2E_SRC%"
>>"%ID%" echo == os ==
>>"%ID%" ver
rem wmic is gone from Windows 11 24H2 (optional feature) and wrote UTF-16 when it existed: CIM via
rem PowerShell, whose redirected output follows the console code page set above (UTF-8).
>>"%ID%" 2>nul powershell -NoProfile -NonInteractive -Command "Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber,OSArchitecture | Format-List | Out-String -Width 200"
>>"%ID%" echo == gpu ==
>>"%ID%" 2>nul powershell -NoProfile -NonInteractive -Command "Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion | Format-List | Out-String -Width 200"
>>"%ID%" echo == sessions ==
>>"%ID%" 2>nul qwinsta
>>"%ID%" echo == REMOTE60_* names still set after clearing REMOTE60_NATIVE_* and REMOTE60_E2E_* (names only, never values) ==
for /f "tokens=1 delims==" %%v in ('set REMOTE60_ 2^>nul') do >>"%ID%" echo %%v
>>"%ID%" echo == location ==
>>"%ID%" echo folder=%HERE%
>>"%ID%" echo work=%WORK%
if "%NETDRIVE%"=="1" >>"%ID%" echo NOTE: %HERE:~0,2% is a network drive; the run was staged to the local folder above and the results copied back.
>>"%ID%" echo mode=%MODE%
>>"%ID%" echo %date% %time%

set "RC=0"
if /i "%MODE%"=="diag" goto :diag
if /i "%MODE%"=="home4" goto :home4
if /i "%MODE%"=="home5" goto :home5
echo [1/2] transitions (6M-1.5M-6M, fps 30-60, E-only ms schedule at 1080p)...
echo      if WGC refuses the invisible window, an ORDINARY 1920x1080 test window with a taskbar entry appears (window-mode=visible-plain): leave it alone, do not close it
"%E2E%" --host "%CAND%" --rc-transitions --window 1920x1080 --seconds 18 --keep-dir --save-frames > "%OUT%\transitions.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\transitions.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if not "%PHASE_RC%"=="0" set "RC=1"
if "%PHASE_RC%"=="3" (
  echo      CAPTURE-UNAVAILABLE: no capture item for the harness window even with the visible fallback -- the matrix is skipped
  >"%OUT%\matrix.exit" echo skipped-capture-unavailable
  goto :done
)
if /i "%MODE%"=="quick" goto :done
echo [2/2] matrix (5 scenes x 30/60 x 1.5/3/6, baseline vs candidate; ~20 min)...
"%E2E%" --host "%CAND%" --baseline "%BASE%" --rc-matrix --window 1920x1080 --seconds 12 --keep-dir --save-frames > "%OUT%\matrix.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\matrix.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if not "%PHASE_RC%"=="0" set "RC=1"
goto :done

:diag
echo [diag] five short phases; a taskbar test window may appear (window-mode=visible-plain): leave it alone
echo [D1/5] control: textscroll 30fps 6M 1080p, candidate, 10 s
"%E2E%" --host "%CAND%" --rc-matrix --rc-only textscroll --rc-fps 30 --rc-rate 6000000 --window 1920x1080 --seconds 10 --keep-dir > "%OUT%\d1_control.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\d1_control.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if not "%PHASE_RC%"=="0" set "RC=1"
if "%PHASE_RC%"=="3" (
  echo      CAPTURE-UNAVAILABLE in the control phase -- nothing else can be measured; stopping
  goto :done
)
echo [D2/5] scaler: textscroll 30fps 1.5M, 1080p page -^> 720p encode through the GPU scaler, candidate then baseline, 10 s each
"%E2E%" --host "%CAND%" --baseline "%BASE%" --rc-matrix --rc-only textscroll --rc-fps 30 --rc-rate 1500000 --rc-order cand-first --window 1920x1080 --seconds 10 --keep-dir > "%OUT%\d2_scaler.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\d2_scaler.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if not "%PHASE_RC%"=="0" set "RC=1"
echo [D3/5] no-scaler: the same cell with a 1280x720 page, candidate then baseline, 10 s each
"%E2E%" --host "%CAND%" --baseline "%BASE%" --rc-matrix --rc-only textscroll --rc-fps 30 --rc-rate 1500000 --rc-order cand-first --window 1280x720 --seconds 10 --keep-dir > "%OUT%\d3_noscaler.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\d3_noscaler.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if not "%PHASE_RC%"=="0" set "RC=1"
echo [D4/5] motion: 60fps 6M 1080p, candidate first then baseline, 12 s each
"%E2E%" --host "%CAND%" --baseline "%BASE%" --rc-matrix --rc-only motion --rc-fps 60 --rc-rate 6000000 --rc-order cand-first --window 1920x1080 --seconds 12 --keep-dir --save-frames > "%OUT%\d4_motion.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\d4_motion.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if not "%PHASE_RC%"=="0" set "RC=1"
echo [D5/5] transitions: down/up, fps, E-only, 16 s each
"%E2E%" --host "%CAND%" --rc-transitions --window 1920x1080 --seconds 16 --keep-dir > "%OUT%\d5_transitions.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\d5_transitions.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if not "%PHASE_RC%"=="0" set "RC=1"
goto :done

:home5
echo [home5] H0 first: five 10 s baseline runs of textscroll 30fps 6M 1080p, one per log destination
set "H0CELL=--rc-matrix --rc-observe --rc-only textscroll --rc-fps 30 --rc-rate 6000000 --window 1920x1080 --seconds 10 --keep-dir"
echo [H0a] stdout to NUL
"%E2E%" --host "%BASE%" %H0CELL% --host-log nul > "%OUT%\h0a_nul.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\h0a_nul.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if "%PHASE_RC%"=="3" (
  echo      CAPTURE-UNAVAILABLE -- nothing else can be measured; stopping
  set "RC=1"
  goto :done
)
echo [H0b] host.log in the local staging dir
"%E2E%" --host "%BASE%" %H0CELL% --host-log run > "%OUT%\h0b_local.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\h0b_local.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if not "%NETDRIVE%"=="1" goto :h0_skip_c
echo [H0c] host.log on the network share %HERE:~0,2%
mkdir "%HERE%results\%STAMP%\h0c_hostlogs" 2>nul
"%E2E%" --host "%BASE%" %H0CELL% --host-log "dir:%HERE%results\%STAMP%\h0c_hostlogs" > "%OUT%\h0c_share.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\h0c_share.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
goto :h0_d
:h0_skip_c
echo [H0c] skipped: %HERE:~0,2% is not a network drive
>"%OUT%\h0c_share.exit" echo skipped-not-network
:h0_d
echo [H0d] local host.log, harness does not read it while the host runs
"%E2E%" --host "%BASE%" %H0CELL% --host-log run --no-log-poll > "%OUT%\h0d_nopoll.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\h0d_nopoll.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
echo [H0e] local host.log, no dump trigger
"%E2E%" --host "%BASE%" %H0CELL% --host-log run --no-dump > "%OUT%\h0e_nodump.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\h0e_nodump.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
rem H0 is discrimination material: its exits are recorded, not summed into the set's result.
echo [home5] H0 done; continuing with the home4 phases

:home4
echo [home4] eight short phases; a taskbar test window may appear (window-mode=visible-plain): leave it alone
set "DIAG=0"
set "INCOMPLETE=0"
rem cmd expands %PHASE_RC% inside a parenthesised block at parse time (it came out empty on the first dry
rem run: "exit " and an "ECHO is off." exit file), so the original-candidate phases use labels, not a block.
if not exist "%ORIGB27%" goto :home4_nob27
echo [H1/8] orig-720p: textscroll 30fps 1.5M through the GPU scaler, ORIGINAL b27 then baseline, 10 s each (diagnostic)
"%E2E%" --host "%ORIGB27%" --baseline "%BASE%" --rc-matrix --rc-diagnostic --rc-only textscroll --rc-fps 30 --rc-rate 1500000 --rc-order cand-first --window 1920x1080 --seconds 10 --keep-dir > "%OUT%\h1_orig_720p.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\h1_orig_720p.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if "%PHASE_RC%"=="4" set "DIAG=1"
if "%PHASE_RC%"=="3" (
  echo      CAPTURE-UNAVAILABLE in the first phase -- nothing else can be measured; stopping
  set "RC=1"
  goto :done
)
if not "%PHASE_RC%"=="0" if not "%PHASE_RC%"=="4" set "RC=1"
goto :home4_sym
:home4_nob27
echo [H1/8] skipped: GNLinkStream.orig-b27.exe is not beside run.cmd -- the original stall is NOT collected; the set is INCOMPLETE
>"%OUT%\h1_orig_720p.exit" echo skipped-no-orig-b27
set "INCOMPLETE=1"
:home4_sym
if not exist "%ORIGSYM%" goto :home4_nosym
echo [H2/8] orig-motion: motion 60fps 6M 1080p, ORIGINAL rebuilt-with-PDB then baseline, 12 s each (diagnostic)
"%E2E%" --host "%ORIGSYM%" --baseline "%BASE%" --rc-matrix --rc-diagnostic --rc-only motion --rc-fps 60 --rc-rate 6000000 --rc-order cand-first --window 1920x1080 --seconds 12 --keep-dir > "%OUT%\h2_orig_motion.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\h2_orig_motion.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if "%PHASE_RC%"=="4" set "DIAG=1"
if not "%PHASE_RC%"=="0" if not "%PHASE_RC%"=="4" set "RC=1"
goto :home4_r9
:home4_nosym
echo [H2/8] skipped: GNLinkStream.orig-sym.exe is not beside run.cmd -- no original dump with names; the set is INCOMPLETE
>"%OUT%\h2_orig_motion.exit" echo skipped-no-orig-sym
set "INCOMPLETE=1"
:home4_r9
echo [H3/8] r9-motion: motion 60fps 6M 1080p, r9 candidate then baseline, 12 s each
"%E2E%" --host "%CAND%" --baseline "%BASE%" --rc-matrix --rc-only motion --rc-fps 60 --rc-rate 6000000 --rc-order cand-first --window 1920x1080 --seconds 12 --keep-dir > "%OUT%\h3_r9_motion.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\h3_r9_motion.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if not "%PHASE_RC%"=="0" set "RC=1"
if "%PHASE_RC%"=="3" (
  echo      CAPTURE-UNAVAILABLE -- nothing else can be measured; stopping
  goto :done
)
echo [H4/8] r9-text: textscroll 60fps 6M 1080p, r9 candidate then baseline, 12 s each
"%E2E%" --host "%CAND%" --baseline "%BASE%" --rc-matrix --rc-only textscroll --rc-fps 60 --rc-rate 6000000 --rc-order cand-first --window 1920x1080 --seconds 12 --keep-dir > "%OUT%\h4_r9_text.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\h4_r9_text.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if not "%PHASE_RC%"=="0" set "RC=1"
echo [H5/8] r9-720p: textscroll 30fps 1.5M through the GPU scaler, r9 candidate then baseline, 10 s each
"%E2E%" --host "%CAND%" --baseline "%BASE%" --rc-matrix --rc-only textscroll --rc-fps 30 --rc-rate 1500000 --rc-order cand-first --window 1920x1080 --seconds 10 --keep-dir > "%OUT%\h5_r9_720p.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\h5_r9_720p.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if not "%PHASE_RC%"=="0" set "RC=1"
echo [H6/8] r9-trans: down/up, fps, E-only transitions, r9 candidate, 16 s each (event frames saved)
"%E2E%" --host "%CAND%" --rc-transitions --window 1920x1080 --seconds 16 --keep-dir --save-frames > "%OUT%\h6_r9_trans.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\h6_r9_trans.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if not "%PHASE_RC%"=="0" set "RC=1"
echo [H7/8] cbr51: motion 60fps 6M with the CBR mode override, r9 candidate only, 12 s (a contrast row; does not decide the set)
set REMOTE60_NATIVE_RC_MODE_NUM=0
"%E2E%" --host "%CAND%" --rc-matrix --rc-only motion --rc-fps 60 --rc-rate 6000000 --window 1920x1080 --seconds 12 --keep-dir > "%OUT%\h7_cbr51_motion.log" 2>&1
set "PHASE_RC=%errorlevel%"
set REMOTE60_NATIVE_RC_MODE_NUM=
>"%OUT%\h7_cbr51_motion.exit" echo %PHASE_RC%
echo      exit %PHASE_RC% (contrast row)
echo [H8/8] text-pgm: singlechange 30fps 6M 1080p, r9 candidate then baseline, 12 s, event frames saved for both
"%E2E%" --host "%CAND%" --baseline "%BASE%" --rc-matrix --rc-only singlechange --rc-fps 30 --rc-rate 6000000 --rc-order cand-first --window 1920x1080 --seconds 12 --keep-dir --save-frames > "%OUT%\h8_text_pgm.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\h8_text_pgm.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if not "%PHASE_RC%"=="0" set "RC=1"
if "%INCOMPLETE%"=="1" set "RC=1"
goto :done

:done
rem The staged harness copy did its job; its hash is in identity.txt, the folder keeps the results only.
del /q "%E2E%" >nul 2>&1
set "HANDBACK=%OUT%"
if not "%NETDRIVE%"=="1" goto :copyback_done
mkdir "%HERE%results" 2>nul
echo copying the results back to %HERE%results\%STAMP% ...
robocopy "%OUT%" "%HERE%results\%STAMP%" /E /NFL /NDL /NJH /NJS /R:2 /W:2 >nul
set "ROBO=%errorlevel%"
call :copyback_verify "%OUT%" "%HERE%results\%STAMP%"
:copyback_done
if "%RC%"=="0" if not "%DIAG%"=="1" echo done, every phase 0. Hand back the folder: %HANDBACK%
if "%RC%"=="0" if "%DIAG%"=="1" echo done: the r9 phases passed; H1/H2 collected the ORIGINAL's stall evidence ^(exit 4 = material, not a pass^). Hand back the folder: %HANDBACK%
if not "%RC%"=="0" echo done with a FAILED, SKIPPED or INCOMPLETE phase ^(see *.exit / FAIL / INVALID / CAPTURE- / HANG-DUMP lines^). Hand back the folder: %HANDBACK%
endlocal & exit /b %RC%

:copyback_verify
rem robocopy exit codes below 8 are success classes; and every phase file of THIS run must be present at the
rem destination (exit files, logs, identity) -- one file's existence is not "copied".
set "SRC=%~1"
set "DST=%~2"
if %ROBO% GEQ 8 (
  echo COPY-BACK FAILED ^(robocopy %ROBO%^): the results are still in %SRC%
  set "RC=1"
  goto :eof
)
set "MISSING=0"
for %%f in ("%SRC%\*.exit" "%SRC%\*.log" "%SRC%\identity.txt") do if not exist "%DST%\%%~nxf" set /a MISSING+=1
for /f %%d in ('dir /s /b /a-d "%SRC%\*.dmp" 2^>nul ^| find /c /v ""') do set "SRCDMP=%%d"
for /f %%d in ('dir /s /b /a-d "%DST%\*.dmp" 2^>nul ^| find /c /v ""') do set "DSTDMP=%%d"
if not "%SRCDMP%"=="%DSTDMP%" set /a MISSING+=1
if "%MISSING%"=="0" (
  echo copied: every phase file and %DSTDMP% dump^(s^) are at %DST%
  set "HANDBACK=%DST%"
) else (
  echo COPY-BACK INCOMPLETE ^(%MISSING% item^(s^) missing^): hand back the LOCAL folder %SRC%
  set "RC=1"
)
goto :eof
