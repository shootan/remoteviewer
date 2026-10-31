"""Classify where two PE files differ, by actual header field -- not by assumption.

A count of differing bytes says nothing on its own. This maps each differing offset to
the PE structure it falls in, so "only the timestamp differs" is something read off the
file rather than assumed from the toolchain.
"""
import struct
import sys


def pe_fields(data):
    """Offsets of the fields a rebuild is expected to change, as (start, length, name)."""
    out = []
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    coff = pe + 4
    out.append((coff + 4, 4, "COFF TimeDateStamp"))
    opt_size, = struct.unpack_from("<H", data, coff + 16)
    opt = coff + 20
    magic, = struct.unpack_from("<H", data, opt)
    plus = magic == 0x20B
    # CheckSum sits at a fixed offset inside the optional header.
    out.append((opt + 64, 4, "OptionalHeader CheckSum"))
    dd = opt + (112 if plus else 96)
    dbg_rva, dbg_size = struct.unpack_from("<II", data, dd + 6 * 8)

    n_sections, = struct.unpack_from("<H", data, coff + 2)
    sec = opt + opt_size
    sections = []
    for i in range(n_sections):
        b = sec + i * 40
        name = data[b:b + 8].rstrip(b"\0").decode("latin1")
        va, = struct.unpack_from("<I", data, b + 12)
        rs, = struct.unpack_from("<I", data, b + 16)
        rp, = struct.unpack_from("<I", data, b + 20)
        sections.append((name, va, rs, rp))

    def to_off(rva):
        for _, va, rs, rp in sections:
            if va <= rva < va + rs:
                return rp + (rva - va)
        return None

    if dbg_rva:
        d = to_off(dbg_rva)
        if d is not None:
            n = dbg_size // 28
            for i in range(n):
                e = d + i * 28
                stamp = e + 4
                out.append((stamp, 4, "DebugDirectory[%d] TimeDateStamp" % i))
                dtype, = struct.unpack_from("<I", data, e + 12)
                sz, = struct.unpack_from("<I", data, e + 16)
                raw, = struct.unpack_from("<I", data, e + 24)
                if dtype == 2 and raw:  # IMAGE_DEBUG_TYPE_CODEVIEW
                    out.append((raw, sz, "CodeView (PDB GUID/age/path)"))
                elif raw:
                    out.append((raw, sz, "DebugData[%d] type=%d" % (i, dtype)))
    return out, sections


def which(off, fields, sections):
    for start, length, name in fields:
        if start <= off < start + length:
            return name
    for name, va, rs, rp in sections:
        if rp <= off < rp + rs:
            return "section %s (unclassified)" % name
    return "outside sections (unclassified)"


def main():
    a = open(sys.argv[1], "rb").read()
    b = open(sys.argv[2], "rb").read()
    if len(a) != len(b):
        print("sizes differ: %d vs %d" % (len(a), len(b)))
        return
    fields, sections = pe_fields(a)
    diffs = [i for i in range(len(a)) if a[i] != b[i]]
    print("differing bytes: %d of %d" % (len(diffs), len(a)))
    buckets = {}
    for off in diffs:
        buckets.setdefault(which(off, fields, sections), []).append(off)
    for name in sorted(buckets):
        offs = buckets[name]
        print("  %-40s %3d bytes  first=0x%X last=0x%X" % (name, len(offs), offs[0], offs[-1]))
    unclassified = sum(len(v) for k, v in buckets.items() if "unclassified" in k)
    print("RESULT:", "all differences are build-identity fields" if unclassified == 0
          else "%d bytes NOT explained by build-identity fields" % unclassified)


main()
