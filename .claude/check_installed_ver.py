import glob, os, hashlib
for f in sorted(glob.glob('C:/Program Files/GNLink/*.exe') + glob.glob('C:/Program Files/GNLink/*.gnlink-old*')):
    b = open(f, 'rb').read()
    v = []
    for s in ('0.2.130', '0.2.131'):
        if s.encode('utf-16-le') in b:
            v.append(s)
    print('%-28s %9d %s %s' % (os.path.basename(f), len(b), hashlib.sha256(b).hexdigest()[:16], ','.join(v) or '-'))
