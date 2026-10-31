p = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src\android_decoder_inbox_prefix_test.cpp'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:90], s.count(a))
    s = s.replace(a, b)


rep('''std::string show(const Verdict& v) {''', '''std::string list(const std::vector<Fed>& in) {
  std::string out;
  for (const Fed& f : in) out += std::to_string(f.seq) + (f.key ? "K " : " ");
  return out;
}
std::string show(const Verdict& v) {''')
rep('''  std::vector<Fed> fa1;
  const Verdict a0 = nobuf(false, 700, &s0, &pp0, 130);''', '''  std::vector<Fed> fa1, fa0;
  const uint32_t a0start = gSeq;
  const Verdict a0 = nobuf(false, 700, &s0, &pp0, 130, &fa0);''')
rep('''  check("[P3 current] none corrupt", a0.corrupt == 0, show(a0));''',
    '''  check("[P3 current] none corrupt", a0.corrupt == 0,
        show(a0) + (a0.corrupt ? " fed(seq,K=IDR) after start " + std::to_string(a0start) + ": " + list(fa0) +
                                     " status: " + s0.substr(s0.find(" inbox="), 400) : std::string()));''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
