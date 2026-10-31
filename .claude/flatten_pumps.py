# -*- coding: utf-8 -*-
"""Removes the nested pumping: no eval() inside a pump_until predicate."""
import io

p = 'apps/native_poc/src/client_update_ui_test.cpp'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- the helper
old = '''bool page_sent(const std::string& type) {'''
new = '''/**
 * Waits for a script to answer with `expected`, without nesting one pump inside another.
 *
 * `eval` already runs a message loop, so using it inside a `pump_until` predicate meant two loops
 * pumping the same queue, one from inside the other. That arrangement ran fine for a while and
 * then fell over part-way through a run, differently each time -- which is what re-entrant
 * dispatch looks like. Here the loop is on the outside and `eval` is called once per turn.
 */
bool wait_for(const std::wstring& script, const std::string& expected, DWORD budgetMs) {
  const DWORD deadline = GetTickCount() + budgetMs;
  for (;;) {
    if (eval(script) == expected) return true;
    if (GetTickCount() > deadline) return false;
    Sleep(50);
  }
}

bool page_sent(const std::string& type) {'''
assert d.count(old) == 1, 'helper anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- replace each nested predicate
subs = [
('''  ok(pump_until(
         []() {
           return eval(L"document.getElementById('updateBar').classList.contains('hidden')") ==
                  "false";
         },
         8000),
     "the offer appears on the shipped page");''',
'''  ok(wait_for(L"document.getElementById('updateBar').classList.contains('hidden')", "false", 8000),
     "the offer appears on the shipped page");'''),

('''  ok(pump_until([]() { return eval(L"document.getElementById('updateNow').disabled") == "true"; },
                5000),
     "while an install is starting the button stays disabled");''',
'''  ok(wait_for(L"document.getElementById('updateNow').disabled", "true", 5000),
     "while an install is starting the button stays disabled");'''),

('''  ok(pump_until([]() { return eval(L"document.getElementById('updateNow').disabled") == "false"; },
                5000),
     "a failed start hands the button back rather than leaving a dead end");''',
'''  ok(wait_for(L"document.getElementById('updateNow').disabled", "false", 5000),
     "a failed start hands the button back rather than leaving a dead end");'''),

('''  pump_until(
      []() {
        return eval(L"document.getElementById('updateBar').classList.contains('hidden')") ==
               "false";
      },
      8000);''',
'''  wait_for(L"document.getElementById('updateBar').classList.contains('hidden')", "false", 8000);'''),

('''  ok(pump_until(
         []() {
           return eval(L"document.getElementById('updateBar').classList.contains('hidden')") ==
                  "true";
         },
         5000),''',
'''  ok(wait_for(L"document.getElementById('updateBar').classList.contains('hidden')", "true", 5000),'''),
]

for old_text, new_text in subs:
    count = d.count(old_text)
    assert count >= 1, 'missing: ' + old_text[:60]
    d = d.replace(old_text, new_text)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('remaining nested evals:', d.count('return eval('))
