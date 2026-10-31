# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/CMakeLists.txt'
d = io.open(p, encoding='utf-8').read()

old = '''add_executable(remote60_update_manifest_test
  src/update_manifest.cpp'''
new = '''# Checks a REAL release manifest with the verifier the product ships, using the compiled-in key.
# Not a test: a tool the release procedure runs against the document about to be published. The
# suite above proves the shipped key accepts a fixture signed by that key, which is a different
# claim from "the product accepts this document".
add_executable(remote60_verify_release
  src/update_manifest.cpp
  src/update_http.cpp
  src/payload_name.cpp
  src/update_signature.cpp
  tools/verify_release_manifest.cpp
)
target_include_directories(remote60_verify_release PRIVATE src)
target_link_libraries(remote60_verify_release PRIVATE bcrypt winhttp)
if(MSVC)
  target_compile_options(remote60_verify_release PRIVATE /utf-8)
endif()

add_executable(remote60_update_manifest_test
  src/update_manifest.cpp'''
assert d.count(old) == 1, 'anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok cmake')
