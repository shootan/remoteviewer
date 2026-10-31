import io

p = 'apps/directory/test/run.js'
d = io.open(p, encoding='utf-8').read()

old = "  const manifestModule = await runTest(['update_manifest_test.js']);"
assert old in d, 'manifest test call'

new = (old + '\n'
       '  if (manifestModule.code !== 0) {\n'
       '    cleanup();\n'
       "    console.log('\\nRESULT: FAILED');\n"
       '    process.exit(manifestModule.code);\n'
       '  }\n'
       '\n'
       '  // The three runtimes all agree with the shared vectors. What that does NOT establish is\n'
       '  // that the document this server actually emits has the same shape -- it could drift and\n'
       '  // every suite would stay green while nothing in the field could install anything.\n'
       "  console.log('\\n--- update publish contract (server output vs shared vectors) ---');\n"
       "  const publishContract = await runTest(['update_publish_contract_test.js']);\n"
       '  if (publishContract.code !== 0) {\n'
       '    cleanup();\n'
       "    console.log('\\nRESULT: FAILED');\n"
       '    process.exit(publishContract.code);\n'
       '  }')

# replace the call plus the existing guard that follows it
start = d.find(old)
after = d.find('}', d.find('process.exit(manifestModule.code);', start)) + 1
assert after > start, 'guard end'
d = d[:start] + new + d[after:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('registered in the runner')
