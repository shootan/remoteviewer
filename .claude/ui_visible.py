# -*- coding: utf-8 -*-
"""The hiding check asked the wrong question.

`classList.contains('hidden')` was true the whole time. The class was on the element and the rule
never applied, because `#updateBar` is an id and `.hidden` is a class. The test agreed with the
script and neither of them looked at the screen.
"""
import io

p = 'apps/native_poc/src/client_update_ui_test.cpp'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- a render-level predicate
old = '''/** The same predicate as a standalone script, for when it is asked on its own. */
std::wstring pressable_script() { return std::wstring(kPressableFn) + L"()"; }'''
new = '''/** The same predicate as a standalone script, for when it is asked on its own. */
std::wstring pressable_script() { return std::wstring(kPressableFn) + L"()"; }

/**
 * Whether the offer is actually on the screen.
 *
 * Asked of the rendered result, not of the class list. `classList.contains('hidden')` was true
 * for the entire life of the bug in 2026-09-11: the class was applied exactly as the script
 * intended and `#updateBar { display: flex }` (an id, 0-1-0-0) beat `.hidden { display: none }`
 * (a class, 0-0-1-0), so the bar was painted from the first frame. A test that reads the class
 * is reading the intention; this reads the outcome.
 */
const wchar_t* kVisibleFn =
    L"(function(){"
    L"  var b=document.getElementById('updateBar');"
    L"  if(!b) return 'no element';"
    L"  var s=getComputedStyle(b);"
    L"  if(s.display==='none') return 'hidden';"
    L"  if(s.visibility!=='visible') return 'visibility:'+s.visibility;"
    L"  if(parseFloat(s.opacity)<0.01) return 'transparent';"
    L"  var r=b.getBoundingClientRect();"
    L"  if(r.width<1||r.height<1) return 'zero size';"
    L"  if(r.bottom<=0||r.top>=innerHeight) return 'off viewport';"
    L"  var hit=document.elementFromPoint(r.left+r.width/2, r.top+r.height/2);"
    L"  if(!hit||!(hit===b||b.contains(hit))) return 'covered';"
    L"  return 'shown';"
    L"})";

std::wstring visible_script() { return std::wstring(kVisibleFn) + L"()"; }'''
assert d.count(old) == 1, 'visible helper anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- every hidden/shown assertion
subs = [
('''  ok(eval(L"document.getElementById('updateBar').classList.contains('hidden')") == "true",
     "before any check, there is no offer on screen");''',
 '''  // Read from the rendering, not the class list -- that distinction is the whole point of this
  // block now.
  const std::string atStart = eval(visible_script());
  ok(atStart == "\\"hidden\\"", "before any check, there is no offer on screen", atStart);'''),

('''  ok(wait_for(L"document.getElementById('updateBar').classList.contains('hidden')", "false", 8000),
     "the offer appears on the shipped page");''',
 '''  ok(wait_for(visible_script(), "\\"shown\\"", 8000), "the offer appears on the shipped page");'''),

('''  wait_for(L"document.getElementById('updateBar').classList.contains('hidden')", "false", 8000);''',
 '''  wait_for(visible_script(), "\\"shown\\"", 8000);'''),

('''  ok(wait_for(L"document.getElementById('updateBar').classList.contains('hidden')", "true", 5000),''',
 '''  ok(wait_for(visible_script(), "\\"hidden\\"", 5000),'''),

('''  ok(eval(L"document.getElementById('updateBar').classList.contains('hidden')") == "true",
     "나중에 takes the offer down");''',
 '''  const std::string afterLater = eval(visible_script());
  ok(afterLater == "\\"hidden\\"", "나중에 takes the offer down", afterLater);'''),
]
for old_text, new_text in subs:
    n = d.count(old_text)
    assert n >= 1, 'missing: ' + old_text[:70]
    d = d.replace(old_text, new_text)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('remaining class-list hiding checks:', d.count("classList.contains('hidden')"))
