import io

p = 'apps/directory/test/update_publish_contract_test.js'
d = io.open(p, encoding='utf-8').read()

d = d.replace("const fs = require('fs');\nconst path = require('path');",
              "const crypto = require('crypto');\nconst fs = require('fs');\nconst path = require('path');", 1)

old = '''// A client asked for one platform must not accept the other's document, whatever else is right
// about it. The signature covers `platform`, so this is not a second opinion -- it is the reason
// the field is inside the signed bytes at all.
const androidToWindows = loadManifest(androidDoc, '0'.repeat(128), '0'.repeat(128), 'windows');
check('an android document is not a windows one', androidToWindows.status !== 'Ok',
      String(androidToWindows.status));
const windowsToAndroid = loadManifest(published, '0'.repeat(128), '0'.repeat(128), 'android');
check('and a windows document is not an android one', windowsToAndroid.status !== 'Ok',
      String(windowsToAndroid.status));'''

new = '''// A client asked for one platform must not accept the other's document, whatever else is right
// about it.
//
// Really signed, and that matters: with a junk signature both of these are refused before the
// platform is ever looked at, and the check would pass without testing anything. The first
// version of this did exactly that -- both said SignatureInvalid, which is the right answer to a
// different question.
const { publicKey, privateKey } = crypto.generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
const jwk = publicKey.export({ format: 'jwk' });
const keyHex = Buffer.concat([Buffer.from(jwk.x, 'base64url'),
                              Buffer.from(jwk.y, 'base64url')]).toString('hex');
const signHex = (doc) => crypto.sign('sha256', Buffer.from(doc, 'utf8'),
                                     { key: privateKey, dsaEncoding: 'ieee-p1363' }).toString('hex');

// The control first: correctly signed and asked for as what it is, each one loads. Without this,
// "refused" below could mean the documents were simply unloadable.
const windowsOk = loadManifest(published, signHex(published), keyHex, 'windows', 'x64');
check('a signed windows document loads as windows', windowsOk.status === 'Ok',
      String(windowsOk.status) + ' ' + (windowsOk.detail || ''));
const androidOk = loadManifest(androidDoc, signHex(androidDoc), keyHex, 'android', 'arm64');
check('a signed android document loads as android', androidOk.status === 'Ok',
      String(androidOk.status) + ' ' + (androidOk.detail || ''));

// And now the property, reached because the signature is real: the platform is what refuses it.
const androidToWindows = loadManifest(androidDoc, signHex(androidDoc), keyHex, 'windows', 'x64');
check('an android document is refused for BEING android, not for its signature',
      androidToWindows.status === 'WrongPlatform', String(androidToWindows.status));
const windowsToAndroid = loadManifest(published, signHex(published), keyHex, 'android', 'arm64');
check('and a windows document for being windows',
      windowsToAndroid.status === 'WrongPlatform', String(windowsToAndroid.status));'''
assert old in d, 'cross-platform block'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('platform gate now actually reached')
