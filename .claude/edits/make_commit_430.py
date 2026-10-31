import json

msg = "\n".join([
    "chore(release): 0.2.104 candidate -- the version bump, and a comment that no longer overclaims",
    "",
    "The Windows stabilisation round is in: the log uploader's busy loop after a 401, the receive",
    "side giving up on a stuck head once and resyncing instead of waiting forever, and the encoder",
    "provenance that closes the gate when the input FIFO overflows. Nine commits, each reviewed and",
    "regressed on its own; this one adds the version and nothing else that runs.",
    "",
    "The product change here is product_version.hpp plus one comment. NoteKeyAccepted called the",
    "accepted key \"the decoder's new reference\", which claims more than the call site can know: the",
    "receiver reports it after the generation gate and the frame gate's Decode verdict, but before",
    "the decode itself. It is an admission boundary. If the decode then fails, the existing reason",
    "4/5/7 recovery handles that, not this list.",
    "",
    "The installer is built once from this tree: dist/GNLinkSetup-0.2.104.exe, 3,475,968 bytes,",
    "sha256 039b3a47d36850292c2049400719ddeb0b17fd39f50ce73dec445e6d5d4dff2f, with 0.2.104 embedded",
    "in GNLinkHost, GNLinkClient and GNLinkSetup and no 0.2.103 left anywhere.",
    "",
    "The evidence behind it is the previous commits' runs, not a fresh sweep, and it is not a clean",
    "one: the two sweep failures (the live host holding UDP 43000, the GDI capture rate) and the two",
    "flaky assertions that predate this work (holdKick, S15) stay recorded as unresolved rather than",
    "offset by passes elsewhere. A06's stage wiring is verified by reading the diff, not by running",
    "it. This candidate is for field-testing confirmed code defects; it does not promise to fix the",
    "11:27 physical stall. history #426.",
    "",
    "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>",
])

payload = {"repo_path": "D:/remote/remote", "message": msg}
with open(r'D:\remote\remote\.claude\edits\git_commit_430.json', 'w', encoding='utf-8') as f:
    json.dump(payload, f, ensure_ascii=False)
print(msg)
