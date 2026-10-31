// One-shot generator for the update-manifest signature test vectors.
// Not committed and not part of the build: it produces fixed data that the C++/JS/Kotlin tests
// then verify against. The private key is used here and discarded -- only the public key, the
// manifest bytes and the signature are written out.
'use strict';
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

const outDir = process.argv[2];
fs.mkdirSync(outDir, { recursive: true });

const { publicKey, privateKey } = crypto.generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
const jwk = publicKey.export({ format: 'jwk' });
const x = Buffer.from(jwk.x, 'base64url');
const y = Buffer.from(jwk.y, 'base64url');
if (x.length !== 32 || y.length !== 32) throw new Error(`bad point size ${x.length}/${y.length}`);

// The manifest is signed as exact bytes, so it is written with LF endings and no trailing
// whitespace games -- whatever is on disk is what the signature covers.
const manifest = [
  '# GNLink update manifest -- TEST DATA, signed with a throwaway key.',
  'schema=1',
  'platform=windows',
  'version=0.2.105',
  'artifact=GNLinkSetup-0.2.105.exe',
  'size=3475968',
  'sha256=039b3a47d36850292c2049400719ddeb0b17fd39f50ce73dec445e6d5d4dff2f',
  '',
].join('\n');

const manifestBytes = Buffer.from(manifest, 'utf8');
// ieee-p1363 gives raw r||s, which is what BCrypt/CNG expects. The DER form Node produces by
// default would have to be unwrapped on the C++ side for no benefit.
const sig = crypto.sign('sha256', manifestBytes, { key: privateKey, dsaEncoding: 'ieee-p1363' });
if (sig.length !== 64) throw new Error(`bad signature size ${sig.length}`);

fs.writeFileSync(path.join(outDir, 'test_manifest.txt'), manifestBytes);
fs.writeFileSync(path.join(outDir, 'test_manifest.sig'), sig.toString('hex') + '\n');
fs.writeFileSync(path.join(outDir, 'test_public_key.txt'),
  Buffer.concat([x, y]).toString('hex') + '\n');

// Self-check before anything is trusted downstream.
const ok = crypto.verify('sha256', manifestBytes, { key: publicKey, dsaEncoding: 'ieee-p1363' }, sig);
const tampered = Buffer.from(manifestBytes);
tampered[tampered.indexOf('0.2.105') + 6] = '6'.charCodeAt(0);
const bad = crypto.verify('sha256', tampered, { key: publicKey, dsaEncoding: 'ieee-p1363' }, sig);
console.log(`self-check: valid=${ok} tampered=${bad} (expect true/false)`);
if (!ok || bad) process.exit(1);
console.log(`wrote to ${outDir}`);
