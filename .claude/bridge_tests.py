import io

p = 'apps/native_poc/src/client_shell_bridge_test.cpp'
d = io.open(p, encoding='utf-8').read()

anchor = '''  std::printf(gFailures == 0 ? "\\nclient_shell_bridge_test: PASS\\n"'''
block = '''  // ---------------------------------------------------------------- the start-up update check
  //
  // What this decides is not cosmetic. The client checks at start-up, and the check runs on a
  // laptop that is often opened somewhere with no route to the server. An outcome that
  // interrupts the user for that teaches them to dismiss the dialog, and the next one -- the one
  // that matters -- gets dismissed with it.

  {
    const ShellUpdateNotice notice = shell_update_notice("UpdateAvailable", "0.2.105", "");
    check("a newer version is worth saying", notice.show);
    check("and it names the version", contains(notice.text, "0.2.105"), notice.text);
    check("and it is logged too", !notice.logLine.empty(), notice.logLine);
  }
  {
    const ShellUpdateNotice notice = shell_update_notice("UpdateAvailable", "", "");
    check("a newer version with no version string still says something",
          notice.show && !notice.text.empty(), notice.text);
  }
  {
    const ShellUpdateNotice notice = shell_update_notice("UpToDate", "", "");
    check("up to date says nothing to the user", !notice.show);
    check("but it is still logged", !notice.logLine.empty(), notice.logLine);
  }
  {
    // The case the whole policy exists for. A start-up check that cannot reach the server must
    // not become a dialog, and it must not be recorded as "up to date" either.
    const ShellUpdateNotice notice = shell_update_notice("Unreachable", "", "timed out");
    check("an unreachable server does not interrupt the user", !notice.show);
    check("and the log does not call it up to date",
          !contains(notice.logLine, "up to date"), notice.logLine);
    check("and the log keeps the reason", contains(notice.logLine, "timed out"), notice.logLine);
  }
  {
    const ShellUpdateNotice notice = shell_update_notice("NotConfigured", "", "no url");
    check("a build that cannot check does not report a fault", !notice.show);
    check("and the log says why", contains(notice.logLine, "no url"), notice.logLine);
  }
  {
    const ShellUpdateNotice notice = shell_update_notice("Rejected", "", "bad signature");
    check("an answer that did not verify is logged, not shown", !notice.show);
    check("and the log says it was ignored", contains(notice.logLine, "ignored"), notice.logLine);
  }
  {
    // An outcome name this build does not know is not a reason to interrupt anyone.
    const ShellUpdateNotice notice = shell_update_notice("SomethingNewer", "9.9.9", "");
    check("an unrecognised outcome shows nothing", !notice.show);
    check("but leaves a trace", contains(notice.logLine, "SomethingNewer"), notice.logLine);
  }

''' + anchor
assert anchor in d, 'bridge test anchor'
d = d.replace(anchor, block, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('bridge tests added')
