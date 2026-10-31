import io

BS = chr(92)
p = 'apps/native_poc/src/directory_retry_test.cpp'
d = io.open(p, encoding='utf-8').read()

bad = '  cfg.cachePath = exe_directory() + "' + BS + 'retry-fixture-" + std::string(label) + ".json";'
good = '  cfg.cachePath = exe_directory() + "' + BS + BS + 'retry-fixture-" + std::string(label) + ".json";'
assert d.count(bad) == 1, 'cachePath'
d = d.replace(bad, good, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('cache path repaired')
