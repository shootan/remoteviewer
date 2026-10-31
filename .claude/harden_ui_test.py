# -*- coding: utf-8 -*-
"""Three reinforcements the verifier asked for, in one pass.

1. pressability instead of `disabled`, with negative controls that must actually fail
2. the page under test pinned to the sha256 the signed manifest names
3. evidence graded, so a sent WebMessage is never quoted as a native action
"""
import io

p = 'apps/native_poc/src/client_update_ui_test.cpp'
d = io.open(p, encoding='utf-8').read()
B = chr(92)

# ---------------------------------------------------------------- includes
old = '''#include "WebView2.h"
#include "client_shell_bridge.hpp"'''
new = '''#include "WebView2.h"
#include "client_shell_bridge.hpp"
#include "update_manifest.hpp"
#include "update_signature.hpp"'''
assert d.count(old) == 1, 'include anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- header, rewritten grading
old = '''// What this does NOT prove, and is not claimed anywhere: that a live check against the real
// server produces this message. That needs an account and a network, and it is a separate step.
// This proves the half that was missing -- that when the message arrives, there is a button, and
// pressing it sends what the native side is waiting for.'''
new = '''// Evidence is graded here, and the grades are not interchangeable:
//
//   (1) the page posts a WebMessage           <- what this test observes
//   (2) the native side receives it and acts  <- NOT observed here
//   (3) a real updater starts                 <- NOT observed here
//
// (1) is never quoted as evidence of (2) or (3). Nor does any of this show that a live check
// against the real server produces the message in the first place: that needs an account and a
// network and is a separate step. What is shown is that the HTML which will be published can put
// a pressable button on screen and emit the message the native side has always been waiting for.
//
// "Pressable" is meant literally. `disabled === false` was the first version of that check and it
// is not enough: a button can be enabled and off-screen, transparent, or underneath something
// else. The three negative controls below cover exactly those, and if they ever stop failing the
// check has become decoration.'''
assert d.count(old) == 1, 'header anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- pressability + hash helpers
old = '''bool page_sent(const std::string& type) {'''
new = '''/**
 * Whether a person could actually press this, expressed as one script.
 *
 * Every clause is here because it can be true while the others are false: an element can be
 * enabled and `display:none`; visible and scrolled out of the viewport; on screen and fully
 * transparent; opaque and covered by something with a higher z-index. `elementFromPoint` is the
 * one that catches the last case, because it answers what the mouse would actually hit.
 */
const wchar_t* kPressableScript =
    L"(function(){"
    L"  var b=document.getElementById('updateNow');"
    L"  if(!b) return 'no element';"
    L"  if(b.disabled) return 'disabled';"
    L"  if(!b.offsetParent) return 'not laid out';"
    L"  var s=getComputedStyle(b);"
    L"  if(s.visibility!=='visible') return 'visibility:'+s.visibility;"
    L"  if(parseFloat(s.opacity)<0.99) return 'opacity:'+s.opacity;"
    L"  var r=b.getBoundingClientRect();"
    L"  if(r.width<1||r.height<1) return 'zero size';"
    L"  if(r.bottom<=0||r.right<=0||r.top>=innerHeight||r.left>=innerWidth) return 'off viewport';"
    L"  var hit=document.elementFromPoint(r.left+r.width/2, r.top+r.height/2);"
    L"  if(!hit||!(hit===b||b.contains(hit))) return 'covered by '+(hit?hit.id||hit.tagName:'nothing');"
    L"  b.focus();"
    L"  if(document.activeElement!==b) return 'cannot take focus';"
    L"  return 'pressable';"
    L"})()";

/** ExecuteScript returns JSON, so a string comes back quoted. */
bool pressable() { return eval(kPressableScript) == "\\"pressable\\""; }

std::string sha256_hex_of_file(const std::wstring& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  const std::string bytes((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
  unsigned char digest[32]{};
  const NTSTATUS hashed =
      BCryptHash(alg, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                 static_cast<ULONG>(bytes.size()), digest, sizeof(digest));
  BCryptCloseAlgorithmProvider(alg, 0);
  if (hashed != 0) return {};
  static const char* kHex = "0123456789abcdef";
  std::string out;
  for (unsigned char byte : digest) {
    out += kHex[byte >> 4];
    out += kHex[byte & 0x0f];
  }
  return out;
}

std::string read_file(const std::wstring& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

bool page_sent(const std::string& type) {'''
assert d.count(old) == 1, 'helper anchor'
d = d.replace(old, new, 1)

old = '''#include <functional>
#include <string>'''
new = '''#include <bcrypt.h>

#include <fstream>
#include <functional>
#include <iterator>
#include <string>'''
assert d.count(old) == 1, 'stdlib anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- disabled -> pressable
old = '''  ok(eval(L"document.getElementById('updateNow').disabled") == "false",
     "the install button is usable");'''
new = '''  ok(pressable(), "the install button is actually pressable",
     eval(kPressableScript));'''
assert d.count(old) == 1, 'pressable anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('part 1 ok')
