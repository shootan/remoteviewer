# -*- coding: utf-8 -*-
"""A third implementation checking the same 0.2.109 signature.

The signer is .NET CNG; the second reader is Node's crypto; this one is OpenSSL through
`cryptography`. Three unrelated stacks agreeing on the same bytes is the evidence. One stack
agreeing with itself is not evidence of anything -- a self-check passes for a signature produced
with the wrong encoding just as happily.

Raw r||s is converted to DER here, because that is what OpenSSL wants and the product stores the
raw form. That conversion is itself the thing worth checking: the raw/DER confusion is silent, and
a build that cannot verify anything looks exactly like a release nobody published.
"""
import io
import sys

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

REL = '.claude/rel/0.2.109'
PUB = ('8709ea70daac6464af4ed0fff1ed7489ec9e9a4a908d48babe5b62753242d9a8'
       '72a4556df0c9ffa2e98dc70e6c553a624a8a235e8c4303488743f37ba240193e')

doc = io.open(REL + '/windows.manifest', 'rb').read()
sigHex = io.open(REL + '/windows.sig', encoding='utf-8').read().strip()
raw = bytes.fromhex(sigHex)

xy = bytes.fromhex(PUB)
key = ec.EllipticCurvePublicNumbers(
    int.from_bytes(xy[:32], 'big'), int.from_bytes(xy[32:], 'big'), ec.SECP256R1()).public_key()
der = utils.encode_dss_signature(int.from_bytes(raw[:32], 'big'), int.from_bytes(raw[32:], 'big'))

bad = 0


def check(what, ok, detail=''):
    global bad
    print('%s  %s%s' % ('PASS' if ok else 'FAIL', what, ('  ' + detail) if detail else ''))
    if not ok:
        bad += 1


check('the signature is raw r||s, 64 bytes', len(raw) == 64, '%d bytes' % len(raw))
check('the key is raw X||Y, 128 hex characters', len(PUB) == 128, '%d chars' % len(PUB))

try:
    key.verify(der, doc, ec.ECDSA(hashes.SHA256()))
    check('openssl accepts the signed 0.2.109 manifest', True)
except Exception as e:
    check('openssl accepts the signed 0.2.109 manifest', False, str(e))

tampered = bytearray(doc)
tampered[len(tampered) // 2] ^= 0x01
try:
    key.verify(der, bytes(tampered), ec.ECDSA(hashes.SHA256()))
    check('...and refuses the same document with one byte changed', False, 'it accepted it')
except Exception:
    check('...and refuses the same document with one byte changed', True)

sys.exit(1 if bad else 0)
