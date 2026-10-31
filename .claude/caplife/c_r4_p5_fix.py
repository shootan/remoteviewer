p = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src\android_decoder_inbox_prefix_test.cpp'
s = open(p, encoding='utf-8').read()
start = s.index('    r.recover(3);\n    const auto in = fed_from(r.q0);\n    const Verdict v = r.verdict({at + 1});')
end = s.index('  std::puts("\\n--- [P6]')
new = '''    r.recover(3);
    const auto in = fed_from(r.q0);
    const uint32_t inflight = r.seqAtStart + 1;  // the AU the worker had taken before the reset (old codec, if any)
    size_t firstNew = in.size();
    bool deltaBeforeKey = false;
    for (size_t i = at; i < in.size(); ++i) {
      if (in[i].seq == inflight) continue;
      if (in[i].key) {
        firstNew = i;
        break;
      }
      deltaBeforeKey = true;
    }
    const Verdict v = firstNew < in.size() ? oracle(std::vector<Fed>(in.begin() + firstNew, in.end()), false, 0)
                                            : Verdict{};
    check("[P5] after the reset no delta reaches the new codec before an IDR (the kept prefix is not fed); from that IDR "
          "on, none corrupt", !deltaBeforeKey && firstNew < in.size() && v.corrupt == 0,
          show(v) + " inputs after the reset point=" + std::to_string(in.size() - at));
  }

'''
s = s[:start] + new + s[end:]
open(p, 'w', encoding='utf-8').write(s)
print('ok')
