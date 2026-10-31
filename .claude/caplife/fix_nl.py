p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_filter_avd.py'
s = open(p, 'rb').read().decode('utf-8')
head = "        fh.write('"
tail = "'.join(l for l in log_lines() if 'desktop_backend' in l"
i = s.index(head + "\r\n" + tail) if (head + "\r\n" + tail) in s else s.index(head + "\n" + tail)
j = s.index(tail, i)
s = s[:i] + head + chr(92) + "n" + s[j:]
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
