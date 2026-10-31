import os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs")
MOVED_DIR = os.path.join(DOCS, "legacy", "2026-09")
moved = sorted(os.listdir(MOVED_DIR))
living = [f for f in os.listdir(DOCS) if f.endswith(".md")]

def patch(path, pairs):
    with open(path, "rb") as h:
        data = h.read()
    orig = data
    for old, new in pairs:
        data = data.replace(old, new)
    if data != orig:
        with open(path, "wb") as h:
            h.write(data)
        return True
    return False

changed = []
# living docs: ](name) -> ](legacy/2026-09/name), ](docs/name) -> ](docs/legacy/2026-09/name)
for f in living:
    pairs = []
    for m in moved:
        mb = m.encode("utf-8")
        pairs.append((b"](" + mb + b")", b"](legacy/2026-09/" + mb + b")"))
        pairs.append((b"](" + mb + b"#", b"](legacy/2026-09/" + mb + b"#"))
        pairs.append((b"](docs/" + mb + b")", b"](docs/legacy/2026-09/" + mb + b")"))
    if patch(os.path.join(DOCS, f), pairs):
        changed.append(f)
# moved docs: links to living docs -> ../../name ; links to ../build-incident etc. left as-is
for m in moved:
    if not m.endswith(".md"):
        continue
    pairs = []
    for f in living:
        fb = f.encode("utf-8")
        pairs.append((b"](" + fb + b")", b"](../../" + fb + b")"))
        pairs.append((b"](" + fb + b"#", b"](../../" + fb + b"#"))
    if patch(os.path.join(MOVED_DIR, m), pairs):
        changed.append("legacy/2026-09/" + m)
print("patched:", len(changed))
for c in changed:
    print(" ", c)
