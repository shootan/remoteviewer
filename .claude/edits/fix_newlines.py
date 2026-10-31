import sys
NL = chr(10)
BS = chr(92)
# 1) the gate test: printf literals that got a real newline instead of \n
p = r'D:\remote\remote\apps\native_poc\src\viewer_frame_gate_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
before = s
s = s.replace('never per frame' + NL + '");', 'never per frame' + BS + 'n");')
s = s.replace('a new wait starts over' + NL + '");', 'a new wait starts over' + BS + 'n");')
assert s != before, 'gate test: nothing replaced'
open(p, 'wb').write(s.encode('utf-8'))
print('fixed gate test printf newlines')
# 2) the stage-3 script: same trap in its printf literal (the script text holds a literal backslash-n
#    inside a non-raw triple-quoted string, which Python would turn into a newline)
p2 = r'D:\remote\remote\.claude\edits\step3_clock.py'
t = open(p2, 'rb').read().decode('utf-8')
t2 = t.replace('still trips' + BS + 'n");', 'still trips' + BS + BS + 'n");')
assert t2 != t, 'step3 script: nothing replaced'
open(p2, 'wb').write(t2.encode('utf-8'))
print('patched step3_clock.py')
