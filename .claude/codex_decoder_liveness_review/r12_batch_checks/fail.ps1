$armRc = 3
if ($null -eq $armRc -or $armRc -ne 0) {
    "NOT RUN: any remaining arm; stopped after the first nonzero/unknown arm rc"
    Remove-Item env:KE_PLAN, env:KE_WATCH_SEC, env:KE_RELAY_TRACE, env:KE_SERVER_JS -ErrorAction SilentlyContinue
    if ($null -eq $armRc) { exit 2 }
    exit ([int]$armRc)
  }
"NEXT_ARM"