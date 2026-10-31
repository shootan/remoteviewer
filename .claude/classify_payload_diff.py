"""Classify differences between two installers per embedded payload item.

The top-level classifier puts most of the difference in .rsrc, which is only useful if you
then look inside: .rsrc holds nine whole files, and each embedded PE carries its own build
identity fields. This extracts each RCDATA blob from both installers and classifies the
difference within that blob, so the answer is per-file and read off the structure.
"""
import importlib.util
import struct
import sys

spec = importlib.util.spec_from_file_location("x", ".claude/extract_rcdata_lib.py")
lib = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lib)

cd = importlib.util.spec_from_file_location("c", ".claude/classify_lib.py")
cls = importlib.util.module_from_spec(cd)
cd.loader.exec_module(cls)

a = open(sys.argv[1], "rb").read()
b = open(sys.argv[2], "rb").read()
fa = lib.extract(a)
fb = lib.extract(b)

total_unexplained = 0
for rid in sorted(lib.MAPPING):
    name = lib.MAPPING[rid]
    x, y = fa.get(rid), fb.get(rid)
    if x is None or y is None:
        print("%-6d %-24s MISSING" % (rid, name))
        continue
    if x == y:
        print("%-6d %-24s identical" % (rid, name))
        continue
    if len(x) != len(y):
        print("%-6d %-24s SIZE DIFFERS %d vs %d" % (rid, name, len(x), len(y)))
        total_unexplained += 1
        continue
    diffs = [i for i in range(len(x)) if x[i] != y[i]]
    try:
        fields, sections = cls.pe_fields(x)
    except Exception as e:
        print("%-6d %-24s %d bytes differ, not a PE (%s)" % (rid, name, len(diffs), e))
        total_unexplained += len(diffs)
        continue
    buckets = {}
    for off in diffs:
        buckets.setdefault(cls.which(off, fields, sections), []).append(off)
    un = sum(len(v) for k, v in buckets.items() if "unclassified" in k)
    total_unexplained += un
    print("%-6d %-24s %3d bytes differ" % (rid, name, len(diffs)))
    for k in sorted(buckets):
        print("           %-38s %3d bytes  first=0x%X" % (k, len(buckets[k]), buckets[k][0]))

print("RESULT: %d bytes inside payload NOT explained by build-identity fields" % total_unexplained)
