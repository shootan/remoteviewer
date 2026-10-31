import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

# ------------------------------------------------- the bootstrap's own pipe name and deadline
edit('apps/native_poc/src/updater_main.cpp', [
 ('#include "updater_options.hpp"',
  '#include "update_credential_channel.hpp"\n#include "update_endpoint.hpp"\n#include "updater_options.hpp"\n#include "url_origin.hpp"'),
 ('''bool run_from_copy(const UpdaterOptions& options, const std::vector<std::wstring>& originalArgs,
                   const std::string& credential) {
  const std::wstring copyPath = updater_copy_path(options.workDir, self_image_path());''',
  '''bool run_from_copy(const UpdaterOptions& options, const std::vector<std::wstring>& originalArgs,
                   const std::string& credential) {
  const std::wstring copyPath = updater_copy_path(options.workDir, self_image_path());
  // The bootstrap's own channel. It cannot forward the one it read from: that pipe was proved
  // against the bootstrap's process id, and the working copy is a different process.
  const std::wstring childPipeName =
      credential.empty() ? std::wstring()
                         : make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());'''),
])

# the copy's command line must name the bootstrap's pipe, not the parent's
p = 'apps/native_poc/src/updater_main.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''    commandLine += L" --running-from-copy";'''
new = '''    commandLine += L" --running-from-copy";
    // The name of the bootstrap's pipe replaces the parent's. Passing the parent's would send the
    // copy to a channel that has already been served and closed.
    if (!childPipeName.empty()) {
      commandLine += L" --credential-pipe \\"";
      commandLine += childPipeName;
      commandLine += L"\\"";
    }'''
assert d.count(old) == 1, 'running-from-copy'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok pipe name in command line')
