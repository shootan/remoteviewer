import io

p = 'apps/native_poc/src/update_effects_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''    c.relaunchRequired = [&]() {
      std::wstring cmd = install + L"\\NoSuchProduct.exe";'''
new = '''    // The OPTIONAL phase, because that is what this block is about: an image the machine does not
    // need failing to come back. It runs after the commit now, which is the point -- the install
    // is already final by the time this fails, and it stays final.
    c.relaunchOptional = [&]() {
      std::wstring cmd = install + L"\\NoSuchProduct.exe";'''
assert old in d, 'relaunch stub'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('assigned to the optional phase')
