from pathlib import Path
import re,hashlib
p=Path(__file__).parent
s=Path('D:/remote/remote/.claude/verify_c2/c3_shoff_batch_verify.ps1').read_text(encoding='utf-8-sig')
s='# Reviewed follow-up: one B -> C pair, plain SH-off120, same live boot and fixed 30s pre-arm resource observation.\n'+s
s=s.replace('A and B NOT RUN','B and C NOT RUN')
arms=r'''$arms = @(
  @{ tag = 'bc-b-shoff'; off = 'C:\work\gnlink-wt\apk-decoder-liveness\apk_out\plain-debug-r5.apk';
     on = 'C:\work\gnlink-wt\apk-inbox-c\apk_out_c2\on-debug-c2.apk';
     offSha = '32bb788f3972cb7b3603d6f0d6907efcd503853dcbfc5b55fadcd427e27579ec' },
  @{ tag = 'bc-c-shoff'; off = 'D:\remote\remote\.claude\verify_c2\plain-debug-2b492ef-verify.apk';
     on = 'C:\work\gnlink-wt\apk-inbox-c\apk_out_c2\on-debug-c2.apk';
     offSha = '9f7b0bd08a252865f45994b65a399cea56ed0c3da5e5964687ee4673528ee07b' })
'''
s,n=re.subn(r'(?ms)^\$arms = @\(\n.*?^\)\n',lambda _:arms,s,count=1)
# The original's closing parenthesis shares the final arm's line.
if n==0:s,n=re.subn(r'(?ms)^\$arms = @\(\n.*?^.*?\}\)\n',lambda _:arms,s,count=1)
assert n==1
needle='  "=== $($a.tag) start $(Now)"'
pre=r'''  # A fixed observation, never a retry-until-a-preferred-load rule. Pre-arm values and treatment load are separate.
  for ($j = 0; $j -lt 7; ++$j) {
    $ls = Listener
    $st = Invoke-Adb @('-s', 'emulator-5554', 'get-state')
    $bt = Invoke-Adb @('-s', 'emulator-5554', 'shell', 'cat', '/proc/sys/kernel/random/boot_id')
    $ld = Invoke-Adb @('-s', 'emulator-5554', 'shell', 'cat', '/proc/loadavg')
    "pre-arm $($a.tag) $(Now) listener[$ls] state[$st] boot_id[$bt] guest_loadavg[$ld]"
    if ($ls -ne $listeners[0] -or $st -ne "rc=0 'device'" -or $bt -ne $boots[0] -or
        $ld -notmatch "^rc=0 '[0-9.]+ [0-9.]+ [0-9.]+ [0-9]+/[0-9]+ [0-9]+'$") {
      "INVALID: pre-arm identity/state/load observation failed -- no arm or repeat"
      Remove-Item env:KE_PLAN, env:KE_WATCH_SEC, env:KE_RELAY_TRACE, env:KE_SERVER_JS -ErrorAction SilentlyContinue
      exit 2
    }
    if ($j -lt 6) { Start-Sleep -Seconds 5 }
  }
'''
assert s.count(needle)==1;s=s.replace(needle,pre+needle)
out=p/'bc_shoff_pair_reviewed.ps1';out.write_text(s,encoding='utf-8',newline='\n')
print(out,hashlib.sha256(out.read_bytes()).hexdigest())
