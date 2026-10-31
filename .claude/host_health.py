import io

p = 'apps/native_poc/src/host_app_main.cpp'
d = io.open(p, encoding='utf-8').read()

# 1) start_log_upload reports whether the uploader actually came on
old = '''void start_log_upload() {
  remote60::native_poc::LogUploadConfig upload;
  upload.directoryUrl = g.cache.directoryUrl;
  upload.hostToken = g.cache.hostToken;
  upload.device = g.cache.machineId;
  upload.identity = g.cache.accountId + "/" + g.cache.machineId;
  std::string reason;
  const bool on = remote60::native_poc::log_upload_configure(upload, &reason);
  std::printf("[gnlink-host] log upload %s %s\\n", on ? "on" : "off", reason.c_str());
}'''
new = '''bool start_log_upload() {
  remote60::native_poc::LogUploadConfig upload;
  upload.directoryUrl = g.cache.directoryUrl;
  upload.hostToken = g.cache.hostToken;
  upload.device = g.cache.machineId;
  upload.identity = g.cache.accountId + "/" + g.cache.machineId;
  std::string reason;
  const bool on = remote60::native_poc::log_upload_configure(upload, &reason);
  std::printf("[gnlink-host] log upload %s %s\\n", on ? "on" : "off", reason.c_str());
  return on;
}

/**
 * The one line an updater reads to decide whether this build came up.
 *
 * An updater cannot learn this by looking at the process list: a host that starts and then fails
 * to do its job is exactly the failure a health check exists for, and its existence proves
 * nothing. So the host says, about itself, which version it is and whether it has reached the
 * directory. Written more than once as the answer becomes known; the reader takes the last.
 *
 * `directory=not-configured` is not a failure. No account signed in means there is nothing to
 * reach, and an updater that rolled back for that would be inventing a fault it did not cause.
 *
 * Format is parsed by update_health.cpp -- keep them together.
 */
void write_health_report(const char* directoryState) {
  append_host_app_log(std::string("[host-app] health version=") + narrow(kProductVersion) +
                      " directory=" + directoryState);
}

/** True once the directory has accepted something from us, so the report is written only once. */
bool gDirectoryReported = false;'''
assert old in d, 'log upload anchor'
d = d.replace(old, new, 1)

# 2) the status timer notices the first accepted batch
old = '''    case WM_TIMER:
      if (wParam == kStatusTimer) {
        refresh_status_text();
        update_tray_tip();
      }
      return 0;'''
new = '''    case WM_TIMER:
      if (wParam == kStatusTimer) {
        refresh_status_text();
        update_tray_tip();
        // The first batch the directory accepts is the observable fact that this build can reach
        // it. Reported once; an updater waiting on it stops waiting here.
        if (!gDirectoryReported && remote60::native_poc::log_upload_status().sentBatches > 0) {
          gDirectoryReported = true;
          write_health_report("ok");
        }
      }
      return 0;'''
assert old in d, 'timer anchor'
d = d.replace(old, new, 1)

# 3) the report at start-up, once the signed-in state is known
old = '''    g.cachePath = directory::default_host_cache_path();
    haveToken = directory::load_host_cache(g.cachePath, &g.cache);
    if (haveToken) start_log_upload();
  }'''
new = '''    g.cachePath = directory::default_host_cache_path();
    haveToken = directory::load_host_cache(g.cachePath, &g.cache);
    bool uploaderOn = false;
    if (haveToken) uploaderOn = start_log_upload();
    // Said as soon as it is known, so an updater is not waiting on a machine that has nothing to
    // report. `pending` means an answer is coming; the other two mean none is.
    write_health_report(uploaderOn ? "pending" : "not-configured");
  }'''
assert old in d, 'startup anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('host health report wired')
