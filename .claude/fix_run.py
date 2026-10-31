import io
p = 'apps/directory/test/run.js'
d = io.open(p, encoding='utf-8').read()
bad = "  console.log('\n--- proxy contamination (own server, own proxy) ---');"
good = "  console.log('" + chr(92) + "n--- proxy contamination (own server, own proxy) ---');"
assert d.count(bad) == 1, 'log line'
d = d.replace(bad, good, 1)
bad2 = "    console.log('\nRESULT: FAILED');\n    process.exit(proxied.code);"
good2 = "    console.log('" + chr(92) + "nRESULT: FAILED');\n    process.exit(proxied.code);"
assert d.count(bad2) == 1, 'result line'
d = d.replace(bad2, good2, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('fixed')
