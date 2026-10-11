@echo off
rem encoder-rc-budget: the Home PC isolated run, one folder, no install.
rem
rem Put beside this file (the verifier assembles them from a fixed build and records each hash):
rem   remote60_fec_single_chunk_host_e2e_test.exe   the harness (it stages a copy of each host per run)
rem   GNLinkStream.candidate.exe                     the candidate host (the commit under test)
rem   GNLinkStream.baseline.exe                      the baseline host (the version the field runs)
rem Nothing is installed, no service is touched and the user's own GNLinkStream keeps running: every
rem host this starts is isolated (own LOCALAPPDATA under the staging folder, input injection off,
rem captures only the harness's invisible window). Results land in .\results\<timestamp>\ and NOTHING
rem else: the harness is copied into that folder before it runs, so its test-scratch (host.log and the
rem event_*.pgm frames per run) is created there and belongs to this run alone.
rem
rem   run.cmd         transitions + matrix (the comparison; ~25 min)
rem   run.cmd quick   transitions only (the transition smoke; a few minutes)
rem Exit code: 0 only when every phase that ran returned 0. Each phase's own exit code is in
rem results\<timestamp>\<phase>.exit; a non-zero there is a FAIL line in that phase's .log.
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
rem
rem What counts as the NVIDIA policy having run (per INSTANCE, in each run's host.log):
rem   "rc-identity ... vendorId=VEN_10DE profile=nvidia_pre_types"
rem   "rc-plan stage=pre-types ... maxQp=44"           the plan went in before the media types
rem   "rc-plan stage=post-types ... setAccepted=1 state=accepted effective=1"
rem                                                    every Set accepted and the readback agrees
rem   and the AU sizes actually moved with E: the stats lines' appliedEncoder changes across the E-only
rem   run while "size=1920x1080" never does, and the sent payload follows it.
rem In the E-only run: "encoder-budget ... action=deferred" then "deferred retarget applied" once.
rem In every matrix row: rcWrites=0 -- this counts reconfigure AND governor writes (the ceiling is
rem static on this profile; a governor write there would be a real finding).
rem win267 is the EXTERNAL wire over a 267 ms window (the receive side's clock), not the probe AU.
rem A run whose profile is not nvidia_pre_types, or whose plan reads back mismatch/unknown, is not
rem evidence for the NVIDIA policy -- report it as what it is.
rem Not covered here (section 5 of the report): FrameGate behaviour, real recovery after a stall,
rem low-rate 1080p DesktopText (below 1.5 Mbps).

setlocal
set "HERE=%~dp0"
set "STAMP=%date:~0,4%%date:~5,2%%date:~8,2%_%time:~0,2%%time:~3,2%%time:~6,2%"
set "STAMP=%STAMP: =0%"
set "OUT=%HERE%results\%STAMP%"
mkdir "%OUT%" 2>nul
if not exist "%OUT%" echo cannot create %OUT% & exit /b 2

rem Nothing inherited may change what the host does: clear EVERY REMOTE60_NATIVE_* override a shell
rem might carry (rate control, governor, async poll, decoder backend, static-scene fps, keyint, ...),
rem so the run measures the product's defaults. The namespace is cleared as a whole; the names that
rem remain under REMOTE60_* afterwards are recorded in identity.txt.
for /f "tokens=1 delims==" %%v in ('set REMOTE60_NATIVE_ 2^>nul') do set "%%v="
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

echo == identity ==                                              > "%OUT%\identity.txt"
certutil -hashfile "%E2E_SRC%" SHA256                              >> "%OUT%\identity.txt"
certutil -hashfile "%CAND%" SHA256                                 >> "%OUT%\identity.txt"
certutil -hashfile "%BASE%" SHA256                                 >> "%OUT%\identity.txt"
wmic path win32_VideoController get Name,DriverVersion /format:list >> "%OUT%\identity.txt" 2>nul
qwinsta                                                             >> "%OUT%\identity.txt" 2>nul
echo == REMOTE60_* still set after clearing REMOTE60_NATIVE_* ==      >> "%OUT%\identity.txt"
>>"%OUT%\identity.txt" 2>nul set REMOTE60_
echo %date% %time%                                                  >> "%OUT%\identity.txt"

set "RC=0"
echo [1/2] transitions (6M-1.5M-6M, fps 30-60, E-only ms schedule at 1080p)...
"%E2E%" --host "%CAND%" --rc-transitions --window 1920x1080 --seconds 18 --keep-dir --save-frames > "%OUT%\transitions.log" 2>&1
set "PHASE_RC=%errorlevel%"
>"%OUT%\transitions.exit" echo %PHASE_RC%
echo      exit %PHASE_RC%
if not "%PHASE_RC%"=="0" set "RC=1"
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
if "%RC%"=="0" (echo done, every phase 0. Hand back the folder: %OUT%) else (echo done with a FAILED phase ^(see *.exit / FAIL lines^). Hand back the folder: %OUT%)
endlocal & exit /b %RC%
