"""Extract RT_RCDATA resources from a PE and compare against the staged payload files.

The installer embeds nine files as RCDATA. Finding the file *name* as a string in the
binary says nothing about whether the file's *bytes* are in there, so this walks the
resource directory, pulls each entry by id, and hashes it.
"""
import hashlib
import struct
import sys

RT_RCDATA = 10

MAPPING = {
    200: "GNLinkHost.exe",
    201: "GNLinkStream.exe",
    202: "GNLinkInputService.exe",
    203: "GNLinkCapture.exe",
    204: "GNLinkClient.exe",
    205: "GNLinkViewer.exe",
    206: "shell.html",
    207: "macro.html",
    208: "GNLinkUpdater.exe",
}


def sections_and_rsrc(data):
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[pe_off:pe_off + 4] == b"PE\0\0", "not a PE"
    coff = pe_off + 4
    n_sections, = struct.unpack_from("<H", data, coff + 2)
    opt_size, = struct.unpack_from("<H", data, coff + 16)
    opt = coff + 20
    magic, = struct.unpack_from("<H", data, opt)
    # Resource directory is data directory index 2; its offset differs by PE32 vs PE32+.
    dd = opt + (96 if magic == 0x10B else 112)
    rsrc_rva, rsrc_size = struct.unpack_from("<II", data, dd + 2 * 8)
    sec = opt + opt_size
    sections = []
    for i in range(n_sections):
        base = sec + i * 40
        va, = struct.unpack_from("<I", data, base + 12)
        raw_size, = struct.unpack_from("<I", data, base + 16)
        raw_ptr, = struct.unpack_from("<I", data, base + 20)
        sections.append((va, raw_size, raw_ptr))
    return sections, rsrc_rva


def rva_to_off(sections, rva):
    for va, raw_size, raw_ptr in sections:
        if va <= rva < va + raw_size:
            return raw_ptr + (rva - va)
    return None


def walk(data, sections, root_off, off, level, wanted_type, found, cur_id=None):
    n_named, n_id = struct.unpack_from("<HH", data, off + 12)
    entries = off + 16
    for i in range(n_named + n_id):
        e = entries + i * 8
        name, child = struct.unpack_from("<II", data, e)
        is_dir = child & 0x80000000
        child_off = root_off + (child & 0x7FFFFFFF)
        if level == 0:
            if name & 0x80000000 or name != wanted_type:
                continue
            walk(data, sections, root_off, child_off, 1, wanted_type, found)
        elif level == 1:
            rid = None if (name & 0x80000000) else name
            if is_dir:
                walk(data, sections, root_off, child_off, 2, wanted_type, found, rid)
        else:
            if not is_dir:
                data_rva, size = struct.unpack_from("<II", data, child_off)
                o = rva_to_off(sections, data_rva)
                if o is not None:
                    found[cur_id] = data[o:o + size]


def extract(data):
    sections, rsrc_rva = sections_and_rsrc(data)
    root = rva_to_off(sections, rsrc_rva)
    found = {}
    walk(data, sections, root, root, 0, RT_RCDATA, found)
    return found


def _unused_main():
    exe, staged = sys.argv[1], sys.argv[2]
    data = open(exe, "rb").read()
    sections, rsrc_rva = sections_and_rsrc(data)
    root = rva_to_off(sections, rsrc_rva)
    found = {}
    walk(data, sections, root, root, 0, RT_RCDATA, found)

    print("%-6s %-24s %10s %10s  %s" % ("id", "expected file", "res size", "file size", "sha256"))
    ok = True
    for rid in sorted(MAPPING):
        name = MAPPING[rid]
        blob = found.get(rid)
        if blob is None:
            print("%-6d %-24s %10s %10s  MISSING" % (rid, name, "-", "-"))
            ok = False
            continue
        disk = open("%s/%s" % (staged, name), "rb").read()
        h1 = hashlib.sha256(blob).hexdigest()
        h2 = hashlib.sha256(disk).hexdigest()
        same = "MATCH" if h1 == h2 else "DIFFER"
        if h1 != h2:
            ok = False
        print("%-6d %-24s %10d %10d  %s %s" % (rid, name, len(blob), len(disk), same, h1[:16]))
    extra = sorted(set(found) - set(MAPPING))
    if extra:
        print("unexpected RCDATA ids:", extra)
        ok = False
    print("RESULT:", "all nine match" if ok else "MISMATCH")


