// Regenerates the shared update-manifest vectors at schema 2.
//
// Schema 2 lists artifacts individually -- name, size, SHA-256, URL -- instead of naming one file.
// The vectors carry three, of DIFFERENT sizes and contents, so a test that reads them cannot pass
// by treating them as interchangeable. GNLinkSetup.exe is one of them, because the installer is a
// member of the package it installs.
//
// Not committed and not part of the build. The private key is used here and discarded; only the
// public key, the document and the signature are written out.
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

// Deliberately different bytes and different lengths.
const files = [
  { name: 'GNLinkHost.exe', body: 'HOST-BINARY-CONTENT-v105' },
  { name: 'ui\\shell.html', body: '<html>shell</html>' },
  { name: 'GNLinkSetup.exe', body: 'SETUP-BINARY-CONTENT-v105-longer-than-the-others' },
];
for (const f of files) {
  f.size = Buffer.byteLength(f.body, 'utf8');
  f.sha256 = crypto.createHash('sha256').update(f.body, 'utf8').digest('hex');
  f.url = `https://updates.example/0.2.105/${f.name.replace(/\\/g, '/')}`;
}

const lines = [
  '# GNLink update manifest -- TEST DATA, signed with a throwaway key.',
  'schema=2',
  'releaseId=r-0.2.105-test',
  'platform=windows',
  'arch=x64',
  'version=0.2.105',
];
for (const f of files) lines.push(`artifact=${f.name}|${f.size}|${f.sha256}|${f.url}`);
const manifest = lines.join('\n') + '\n';

const manifestBytes = Buffer.from(manifest, 'utf8');
const sig = crypto.sign('sha256', manifestBytes, { key: privateKey, dsaEncoding: 'ieee-p1363' });
if (sig.length !== 64) throw new Error(`bad signature size ${sig.length}`);

fs.writeFileSync(path.join(outDir, 'test_manifest.txt'), manifestBytes);
fs.writeFileSync(path.join(outDir, 'test_manifest.sig'), sig.toString('hex') + '\n');
fs.writeFileSync(path.join(outDir, 'test_public_key.txt'),
  Buffer.concat([x, y]).toString('hex') + '\n');
// The bodies too, so a test can stage real files with the exact hashes the manifest claims.
fs.mkdirSync(path.join(outDir, 'files'), { recursive: true });
for (const f of files) {
  fs.writeFileSync(path.join(outDir, 'files', f.name.replace(/\\/g, '_')), f.body);
}

const ok = crypto.verify('sha256', manifestBytes, { key: publicKey, dsaEncoding: 'ieee-p1363' }, sig);
const tampered = Buffer.from(manifestBytes);
tampered[tampered.indexOf('0.2.105') + 6] = '6'.charCodeAt(0);
const bad = crypto.verify('sha256', tampered, { key: publicKey, dsaEncoding: 'ieee-p1363' }, sig);
console.log(`self-check: valid=${ok} tampered=${bad} (expect true/false)`);
if (!ok || bad) process.exit(1);
console.log(`wrote schema 2 vectors with ${files.length} artifacts to ${outDir}`);
