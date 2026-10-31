import os
p = r'D:\remote\remote\.claude\edits\step26_conditions.py'
s = open(p, 'rb').read().decode('utf-8')
start = s.index('(r"""#include "host_input_target_rect.hpp"\n#include "secure_input_mapping.hpp')
end = s.index('(r"""// Window mode never routes to the agent; the rect is zero so the agent\'s own virtual-screen')
s = s[:start] + s[end:]
open(p, 'wb').write(s.encode('utf-8'))
print('include pair removed from step26')
