@echo off
rem encoder-rc-budget: the Home PC isolated run, one folder, no install.
rem
rem Put beside this file (the verifier assembles them from a fixed build and records each hash):
rem   remote60_fec_single_chunk_host_e2e_test.exe   the harness (it stages a copy of each host per run)
rem   GNLinkStream.candidate.exe                     the candidate host (the commit under test)
rem   GNLinkStream.baseline.exe                      the baseline host (the version the field runs)
rem Nothing is installed, no service is touched and the user's own GNLinkStream keeps running: every
rem host this starts is isolated (own LOCALAPPDATA under the staging folder, input injection off,
rem captures only the harness's invisible window). Results land in .\results\<timestamp>\.
rem
rem What one run collects (see the .log files; host.log per run under results\...\test-scratch):
rem   transitions.log  6M->1.5M->6M (R at once from the host's own apply marker, E per instance, same
rem                    target twice), fps 30->60, and the E-ONLY 6M<->5M<->5.5M sequence at a fixed
rem                    1080p geometry (coalescing cancel / latest target / repeat no-op).
rem   matrix.log       5 scenes x 30/60 fps x 1.5/3/6 Mbps, baseline beside candidate: wire windows
rem                    (1 s / 250 ms / 267 ms) against R, the host's R/E and plan readback state per
rem                    instance, realtime decode gaps, event-matched change->picture latency, matched
rem                    PSNR (whole picture + text rows), IDR sizes, payload actually sent.
rem   event_*.pgm      the first decoded frame of each change event (visible Y plane) beside that run's
rem                    host.log -- the actual text the viewer got, for a human to read.
rem On an NVIDIA host the lines to look for are: "rc-identity ... vendorId=VEN_10DE profile=nvidia_pre_types",
rem "rc-plan stage=pre-types ... maxQp=44" and "rc-plan stage=post-types ... state=accepted" for EVERY
rem instance, "encoder-budget ... action=deferred" / "deferred retarget applied" in the E-only run, and
rem rcWrites=0 in every matrix row. A run whose profile is not nvidia_pre_types, or whose plan reads
rem back mismatch/unknown, is not evidence for the NVIDIA policy -- report it as what it is.

setlocal
set "HERE=%~dp0"
for /f "tokens=1-3 delims=/:. " %%a in ("%date% %time%") do set "STAMP=%%a%%b%%c"
set "STAMP=%date:~0,4%%date:~5,2%%date:~8,2%_%time:~0,2%%time:~3,2%%time:~6,2%"
set "STAMP=%STAMP: =0%"
set "OUT=%HERE%results\%STAMP%"
mkdir "%OUT%" 2>nul

rem The harness starts real hosts: this is the explicit opt-in it requires.
set REMOTE60_ALLOW_HOST_E2E=1
rem Standalone: the harness's scratch root is normally pinned at build time inside the build machine's
rem repository and refused anywhere else. This folder IS the root here: test-scratch is created beside
rem the exe (same guards: must be created by this run, no links in the way). Test-support switch only;
rem the product binaries do not read it.
set REMOTE60_TEST_SCRATCH_STANDALONE=1
rem "run.cmd quick" runs the transitions only (a few minutes); the default also runs the matrix.
set "QUICK=%~1"
rem Nothing inherited may change what the host does: clear every rate-control / diagnostic override
rem a shell might carry, so the run measures the product's defaults.
set REMOTE60_NATIVE_RATE_CONTROL=
set REMOTE60_NATIVE_PEAK_BITRATE_PERCENT=
set REMOTE60_NATIVE_RC_MODE_NUM=
set REMOTE60_NATIVE_RC_MEAN_OVERRIDE=
set REMOTE60_NATIVE_RC_BEFORE_TYPES=
set REMOTE60_NATIVE_MAX_QP=
set REMOTE60_NATIVE_RATE_GOVERNOR=
set REMOTE60_NATIVE_ENCODER_TUNE_MODE=
set REMOTE60_NATIVE_ENCODER_BACKEND=
set REMOTE60_NATIVE_WIRE_CAP=
set REMOTE60_NATIVE_FORCE_CLAMP_BURST=
set REMOTE60_NATIVE_FEC_SINGLE_CHUNK_STRIDE=
set REMOTE60_NATIVE_NACK_RESERVE_PERCENT=
set REMOTE60_NATIVE_ENCODER_OVERSHOOT_PERCENT=
set REMOTE60_NATIVE_KEYINT_OVERRIDE=
set REMOTE60_NATIVE_LOW_LATENCY=
set REMOTE60_NATIVE_REALTIME=
set REMOTE60_NATIVE_QUALITY_VS_SPEED=

set "E2E=%HERE%remote60_fec_single_chunk_host_e2e_test.exe"
set "CAND=%HERE%GNLinkStream.candidate.exe"
set "BASE=%HERE%GNLinkStream.baseline.exe"
if not exist "%E2E%" echo missing %E2E% & exit /b 2
if not exist "%CAND%" echo missing %CAND% & exit /b 2
if not exist "%BASE%" echo missing %BASE% & exit /b 2

echo == identity ==                                              > "%OUT%\identity.txt"
certutil -hashfile "%E2E%" SHA256                                  >> "%OUT%\identity.txt"
certutil -hashfile "%CAND%" SHA256                                 >> "%OUT%\identity.txt"
certutil -hashfile "%BASE%" SHA256                                 >> "%OUT%\identity.txt"
wmic path win32_VideoController get Name,DriverVersion /format:list >> "%OUT%\identity.txt" 2>nul
qwinsta                                                             >> "%OUT%\identity.txt" 2>nul
echo %date% %time%                                                  >> "%OUT%\identity.txt"

echo [1/2] transitions (6M-1.5M-6M, fps 30-60, E-only 6M-5M-5.5M at 1080p)...
"%E2E%" --host "%CAND%" --rc-transitions --window 1920x1080 --seconds 18 --keep-dir --save-frames > "%OUT%\transitions.log" 2>&1
echo      exit %errorlevel%
if /i "%QUICK%"=="quick" goto :collect
echo [2/2] matrix (5 scenes x 30/60 x 1.5/3/6, baseline vs candidate; ~20 min)...
"%E2E%" --host "%CAND%" --baseline "%BASE%" --rc-matrix --window 1920x1080 --seconds 12 --keep-dir --save-frames > "%OUT%\matrix.log" 2>&1
echo      exit %errorlevel%
:collect

rem The kept run directories (host.log + event_*.pgm per run) live under the harness's test-scratch next
rem to the exe; move them beside the logs so the folder is self-contained to hand back.
if exist "%HERE%test-scratch" move "%HERE%test-scratch" "%OUT%\test-scratch" >nul 2>&1
echo done. Hand back the folder: %OUT%
endlocal
