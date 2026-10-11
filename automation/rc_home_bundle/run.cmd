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
rem Exit code: 0 only when every phase that ran returned 0. Each phase's own exit code is in
rem results\<timestamp>\<phase>.exit; 1 there means FAIL lines in that phase's .log; 3 means the
rem harness could not get a capture item for its window even with the visible fallback (see below) --
rem then the matrix is NOT run (matrix.exit says skipped) and this script returns 1.
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
rem                    PSNR (whole picture + text rows), IDR sizes, payload actually sent.
rem   test-scratch\    per run: host.log and event_*.pgm (the first decoded frame of each change event,
rem                    visible Y plane) -- the actual text the viewer got, for a human to read.
rem   identity.txt     hashes of the three exes (hash lines only), OS version/build (CIM), GPU + driver
rem                    (CIM), session list, the REMOTE60_* variable NAMES still set, folder + a
rem                    network-drive note. One encoding: UTF-8 (chcp 65001), ASCII in practice.
rem
rem What counts as the NVIDIA policy having run (per INSTANCE, in each run's host.log):
rem   "rc-identity ... vendorId=VEN_10DE profile=nvidia_pre_types"
rem   "rc-plan stage=pre-types ... maxQp=44"           the plan went in before the media types
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

setlocal
chcp 65001 >nul
set "HERE=%~dp0"
set "STAMP=%date:~0,4%%date:~5,2%%date:~8,2%_%time:~0,2%%time:~3,2%%time:~6,2%"
set "STAMP=%STAMP: =0%"
set "OUT=%HERE%results\%STAMP%"
mkdir "%OUT%" 2>nul
if not exist "%OUT%" echo cannot create %OUT% & exit /b 2

rem Nothing inherited may change what the host does: clear EVERY REMOTE60_NATIVE_* override a shell
rem might carry (rate control, governor, async poll, decoder backend, static-scene fps, keyint, ...),
rem so the run measures the product's defaults. The namespace is cleared as a whole; the NAMES that
rem remain under REMOTE60_* afterwards are recorded in identity.txt -- names only: a shell may carry
rem REMOTE60_DIR_ADMIN_KEY / REMOTE60_SIGNUP_KEY and the like, and no value of any variable may end
rem up in the folder that is handed back. The harness's own test-support probe (REMOTE60_E2E_FIXTURE_PROBE)
rem is cleared too: a Home run measures the real window, not a forced failure.
for /f "tokens=1 delims==" %%v in ('set REMOTE60_NATIVE_ 2^>nul') do set "%%v="
set REMOTE60_E2E_FIXTURE_PROBE=
rem The harness starts real hosts: this is the explicit opt-in it requires.
set REMOTE60_ALLOW_HOST_E2E=1
rem Standalone: the harness's scratch root is normally pinned at build time inside the build machine's
rem repository and refused anywhere else. Here the folder the harness runs from IS the root (the copy
rem under results\<timestamp>\): test-scratch is created beside that copy (same guards: must be
rem created by this run, no links in the way). Test-support switch only; the product binaries do not
rem read it.
set REMOTE60_TEST_SCRATCH_STANDALONE=1
set "QUICK=%~1"

set "E2E_SRC=%HERE%remote60_fec_single_chunk_host_e2e_test.exe"
set "CAND=%HERE%GNLinkStream.candidate.exe"
set "BASE=%HERE%GNLinkStream.baseline.exe"
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
>>"%ID%" echo == os ==
>>"%ID%" ver
rem wmic is gone from Windows 11 24H2 (optional feature) and wrote UTF-16 when it existed: CIM via
rem PowerShell, whose redirected output follows the console code page set above (UTF-8).
>>"%ID%" 2>nul powershell -NoProfile -NonInteractive -Command "Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber,OSArchitecture | Format-List | Out-String -Width 200"
>>"%ID%" echo == gpu ==
>>"%ID%" 2>nul powershell -NoProfile -NonInteractive -Command "Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion | Format-List | Out-String -Width 200"
>>"%ID%" echo == sessions ==
>>"%ID%" 2>nul qwinsta
>>"%ID%" echo == REMOTE60_* names still set after clearing REMOTE60_NATIVE_* (names only, never values) ==
for /f "tokens=1 delims==" %%v in ('set REMOTE60_ 2^>nul') do >>"%ID%" echo %%v
>>"%ID%" echo == location ==
>>"%ID%" echo folder=%HERE%
net use %HERE:~0,2% >nul 2>&1 && >>"%ID%" echo WARNING: running from a network drive (%HERE:~0,2%). The measurement does not depend on it; recorded because file timing differs.
>>"%ID%" echo %date% %time%

set "RC=0"
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
if /i "%QUICK%"=="quick" goto :done
echo [2/2] matrix (5 scenes x 30/60 x 1.5/3/6, baseline vs candidate; ~20 min)...
"%E2E%" --host "%CAND%" --baseline "%BASE%" --rc-matrix --window 1920x1080 --seconds 12 --keep-dir --save-frames > "%OUT%\matrix.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\matrix.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if not "%PHASE_RC%"=="0" set "RC=1"
:done

rem The staged harness copy did its job; its hash is in identity.txt, the folder keeps the results only.
del /q "%E2E%" >nul 2>&1
if "%RC%"=="0" (echo done, every phase 0. Hand back the folder: %OUT%) else (echo done with a FAILED or SKIPPED phase ^(see *.exit / FAIL / CAPTURE- lines^). Hand back the folder: %OUT%)
endlocal & exit /b %RC%
