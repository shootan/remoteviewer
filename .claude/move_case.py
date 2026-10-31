import io

p = 'apps/native_poc/src/update_handoff_process_test.cpp'
d = io.open(p, encoding='utf-8').read()

start = d.find('  // ---------------------------------------------------------------- the owner changed')
assert start > 0, 'start'
end = d.find('  }\n', d.find('a.why);', d.find('the worker was never told to proceed', start))) + 4
assert end > start, 'end'
block = d[start:end]
d = d[:start] + d[end:]

block = block.replace(
    '    // The witness is what the worker writes when it was told to go ahead. Nothing may be there.\n'
    '    check("...and the worker was never told to proceed", !a.workerProceeded, a.why);\n',
    "    // The witness is the worker's own account of what it decided. It must have stood down: an\n"
    '    // acknowledgement that was never sent cannot have told it to stop anything.\n'
    '    check("...and the worker stood down instead of stopping the product",\n'
    '          a.witness.find("worker stood down") != std::string::npos, a.witness);\n')

anchor = '  return gFailures == 0 ? 0 : 1;'
at = d.rfind(anchor)
assert at > 0, 'anchor'
line_start = d.rfind('\n', 0, d.rfind('\n', 0, at)) + 1
d = d[:line_start] + block + d[line_start:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('moved')
