import sys

def edit(path, pairs):
    raw = open(path, 'rb').read()
    bom = raw.startswith(b'\xef\xbb\xbf')
    if bom:
        raw = raw[3:]
    crlf = b'\r\n' in raw
    text = raw.decode('utf-8').replace('\r\n', '\n')
    for old, new in pairs:
        n = text.count(old)
        if n != 1:
            print(f'FAIL {path}: expected 1 occurrence, found {n} for:\n{old[:200]}')
            sys.exit(1)
        text = text.replace(old, new)
    out = text.replace('\n', '\r\n') if crlf else text
    data = out.encode('utf-8')
    if bom:
        data = b'\xef\xbb\xbf' + data
    open(path, 'wb').write(data)
    print(f'ok {path} ({len(pairs)} edits, crlf={crlf}, bom={bom})')
