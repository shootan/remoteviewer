import sys

def append_history(path, entry_path):
    raw = open(path, 'rb').read()
    bom = raw.startswith(b'\xef\xbb\xbf')
    body = raw[3:] if bom else raw
    crlf = b'\r\n' in body
    text = body.decode('utf-8').replace('\r\n', '\n')
    entry = open(entry_path, 'rb').read().decode('utf-8').replace('\r\n', '\n')
    if not text.endswith('\n'):
        text += '\n'
    text += '\n' + entry.rstrip('\n') + '\n'
    out = text.replace('\n', '\r\n') if crlf else text
    data = out.encode('utf-8')
    if bom:
        data = b'\xef\xbb\xbf' + data
    open(path, 'wb').write(data)
    print(f'appended {entry_path} -> {path} (crlf={crlf}, bom={bom})')

if __name__ == '__main__':
    append_history(sys.argv[1], sys.argv[2])
