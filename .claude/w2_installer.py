import io

BS = chr(92)

# ---------------------------------------------------------------- resource id
p = 'apps/native_poc/installer/installer_ids.h'
d = io.open(p, encoding='utf-8').read()
old = '#define IDR_PAYLOAD_MACRO_UI 207'
new = '''#define IDR_PAYLOAD_MACRO_UI 207
// The updater. A payload file like any other, deliberately: a binary installed outside the
// product directory could never be replaced by an update, because payload names are relative and
// refuse traversal -- it would be frozen at its compile-time constants forever. It runs from a
// copy of itself instead. See docs/업데이트_배선_계획.md W2a.
#define IDR_PAYLOAD_UPDATER 208'''
assert old in d, 'ids anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- rc template
p = 'apps/native_poc/installer/payload.rc.in'
d = io.open(p, encoding='utf-8').read()
old = 'IDR_PAYLOAD_MACRO_UI      RCDATA "@GNLINK_PAYLOAD_MACRO_UI@"'
new = old + '\nIDR_PAYLOAD_UPDATER       RCDATA "@GNLINK_PAYLOAD_UPDATER@"'
assert old in d, 'rc anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- cmake staging
p = 'apps/native_poc/CMakeLists.txt'
d = io.open(p, encoding='utf-8').read()
old = 'set(GNLINK_PAYLOAD_MACRO_UI "${_gnlink_payload_dir}/macro.html")'
new = old + '\nset(GNLINK_PAYLOAD_UPDATER "${_gnlink_payload_dir}/GNLinkUpdater.exe")'
assert old in d, 'cmake var anchor'
d = d.replace(old, new, 1)

old = '''             GNLINK_PAYLOAD_CLIENT_SHELL GNLINK_PAYLOAD_CLIENT_VIEWER GNLINK_PAYLOAD_CLIENT_UI
             GNLINK_PAYLOAD_MACRO_UI)'''
new = '''             GNLINK_PAYLOAD_CLIENT_SHELL GNLINK_PAYLOAD_CLIENT_VIEWER GNLINK_PAYLOAD_CLIENT_UI
             GNLINK_PAYLOAD_MACRO_UI GNLINK_PAYLOAD_UPDATER)'''
assert old in d, 'cmake foreach anchor'
d = d.replace(old, new, 1)

old = '''    "${_gnlink_payload_dir}/shell.html"
    "${_gnlink_payload_dir}/macro.html"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_gnlink_payload_dir}"'''
new = '''    "${_gnlink_payload_dir}/shell.html"
    "${_gnlink_payload_dir}/macro.html"
    "${_gnlink_payload_dir}/GNLinkUpdater.exe"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_gnlink_payload_dir}"'''
assert old in d, 'cmake outputs anchor'
d = d.replace(old, new, 1)

old = '''  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:remote60_host_app>" "${_gnlink_payload_dir}/GNLinkHost.exe"'''
new = '''  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:remote60_updater>" "${_gnlink_payload_dir}/GNLinkUpdater.exe"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:remote60_host_app>" "${_gnlink_payload_dir}/GNLinkHost.exe"'''
assert old in d, 'cmake copy anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('resource, template and staging wired')
