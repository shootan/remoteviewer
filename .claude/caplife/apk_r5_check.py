import re, sys, zipfile, hashlib
for apk in sys.argv[1:]:
    z = zipfile.ZipFile(apk)
    print('==', apk.split('\\')[-1], hashlib.sha256(open(apk,'rb').read()).hexdigest())
    for n in sorted(x for x in z.namelist() if x.endswith('.so') and 'remote60' in x):
        b = z.read(n)
        print('  %-48s trace=%d seam=%d r5=%d r4=%d' % (n, b.count(b'liveness-trace'), b.count(b'sh-test-seam'),
              b.count(b'selection, binding or stream'), b.count(b'during the dequeue')))
    dex = b''.join(z.read(n) for n in z.namelist() if re.match(r'classes\d*\.dex$', n))
    print('  dex 10.0.2.2:18410=%d gnlink.shotan.net=%d' % (dex.count(b'http://10.0.2.2:18410'), dex.count(b'https://gnlink.shotan.net')))
