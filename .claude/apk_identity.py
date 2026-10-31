"""Read an APK's signer certificate and versionCode from the file itself.

Two claims decide whether a candidate can overwrite an installed app -- same signer, and a
higher versionCode. Neither is visible in META-INF when the APK is signed with v2/v3 only,
so this parses the APK Signing Block and the binary manifest rather than trusting a tool's
summary or the absence of a JAR signature.
"""
import hashlib
import struct
import sys
import zipfile

MAGIC = b"APK Sig Block 42"
V2, V3 = 0x7109871A, 0xF05368C0


def signing_block(data):
    # locate EOCD, then the central directory it points at; the signing block sits just before it
    i = data.rfind(b"PK\x05\x06")
    if i < 0:
        return None
    cd_off = struct.unpack_from("<I", data, i + 16)[0]
    if cd_off < 24 or data[cd_off - 16:cd_off] != MAGIC:
        return None
    size_end = struct.unpack_from("<Q", data, cd_off - 24)[0]
    start = cd_off - 8 - size_end
    size_start = struct.unpack_from("<Q", data, start)[0]
    if size_start != size_end:
        return None
    body = data[start + 8:cd_off - 24]
    pairs, off = {}, 0
    while off + 12 <= len(body):
        ln = struct.unpack_from("<Q", body, off)[0]
        pid = struct.unpack_from("<I", body, off + 8)[0]
        pairs[pid] = body[off + 12:off + 8 + ln]
        off += 8 + ln
    return pairs


def lv(buf, off):
    n = struct.unpack_from("<I", buf, off)[0]
    return buf[off + 4:off + 4 + n], off + 4 + n


def certs_from(scheme_block):
    out = []
    signers, _ = lv(scheme_block, 0)
    off = 0
    while off < len(signers):
        signer, off = lv(signers, off)
        signed_data, _ = lv(signer, 0)
        # signed data: digests (len-prefixed), then certificates (len-prefixed)
        _, p = lv(signed_data, 0)
        certs, _ = lv(signed_data, p)
        c = 0
        while c < len(certs):
            cert, c = lv(certs, c)
            out.append(cert)
    return out


def version_code(axml):
    # chunked binary XML; walk to the first StartElement and read its typed attributes,
    # matching by resource id rather than by name string
    pos, resmap = 8, []
    while pos + 8 <= len(axml):
        ctype, hsize, csize = struct.unpack_from("<HHI", axml, pos)
        if csize == 0:
            break
        if ctype == 0x0180:
            n = (csize - hsize) // 4
            resmap = list(struct.unpack_from("<%dI" % n, axml, pos + hsize))
        elif ctype == 0x0102:
            # attributeStart is measured from the attrExt struct, which follows the full
            # 16-byte node header -- not from the 8-byte chunk header.
            a_start, a_size, a_count = struct.unpack_from("<HHH", axml, pos + 16 + 8)
            base = pos + 16 + a_start
            for i in range(a_count):
                a = base + i * a_size
                name_idx = struct.unpack_from("<I", axml, a + 4)[0]
                data = struct.unpack_from("<I", axml, a + 16)[0]
                if name_idx < len(resmap) and resmap[name_idx] == 0x0101021B:
                    return data
        pos += csize
    return None


for path in sys.argv[1:]:
    raw = open(path, "rb").read()
    print("=== %s ===" % path)
    pairs = signing_block(raw)
    if not pairs:
        print("    no APK Signing Block")
    else:
        print("    schemes present:", ", ".join(
            n for n, i in (("v2", V2), ("v3", V3)) if i in pairs) or "none of v2/v3")
        for name, pid in (("v2", V2), ("v3", V3)):
            if pid not in pairs:
                continue
            for cert in certs_from(pairs[pid]):
                print("    %s signer cert sha256 = %s" % (name, hashlib.sha256(cert).hexdigest()))
    z = zipfile.ZipFile(path)
    print("    versionCode =", version_code(z.read("AndroidManifest.xml")))
    print("    v1 (JAR) signature files:",
          [n for n in z.namelist() if n.upper().startswith("META-INF/")
           and n.upper().endswith((".RSA", ".DSA", ".EC"))] or "none")
