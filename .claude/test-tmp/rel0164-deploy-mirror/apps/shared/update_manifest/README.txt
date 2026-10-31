Update manifest format (schema 2)
=================================

What the server publishes to say a release exists, and the only thing that decides whether a
machine is behind. Design: docs/업데이트_기능_설계.md sections 4.1-4.4.

Why there is no archive
-----------------------

A release is several files, and the obvious way to ship several files is a zip. This does not.
The manifest lists each file with its name, size, SHA-256 and URL, and the updater fetches and
verifies them one at a time. The manifest IS the container; it holds identities rather than bytes.

What that buys is narrow and worth stating precisely: there is no compression or extraction code,
so there is nothing there to get wrong. It does NOT mean the attack surface is gone. The names and
the URLs in this document are still input on their way to becoming filesystem paths and network
requests, which is exactly why both are checked -- names against the payload-name rules, URLs
against the HTTPS policy -- before a verified manifest exists at all.

The document
------------

Line-oriented `key=value` UTF-8 text. Not JSON, for two reasons:

  * The signature covers the document's exact bytes. A format with no canonicalisation step has
    nothing to get wrong between the signer and the verifier -- no key ordering, no whitespace
    rules, no number formatting.
  * The same document is read by C++, JavaScript and Kotlin without dragging a parser dependency
    into any of them.

Parsing rules:

  * Blank lines and lines whose first non-space character is `#` are ignored.
  * Leading and trailing spaces and tabs are stripped from key and value; a trailing CR is
    stripped, so a document that travelled as CRLF still parses.
  * A line with no `=` is an error.
  * Unknown keys are IGNORED, not rejected -- a newer server adding a field must not brick an
    older client. Incompatible changes are gated by `schema` instead.

Fields
------

  schema       required. 2. Schema 1 named a single artifact and was never published; it is now
               reported as UnsupportedSchema rather than guessed at.
  releaseId    required. One opaque identity for the whole release. It exists so a set of files
               can be pinned together: fetching each file against whatever "latest" says at the
               moment it is fetched would let a release change underneath an update in progress
               and produce an install that is half one build and half another.
  platform     required. `windows` or `android`. Another platform is WrongPlatform, which is not
               an error -- just not for us.
  arch         required. `x64`, `arm64`, ... Also reported as WrongPlatform when it does not match.
  version      required. Compared by the shared contract in apps/shared/version_compare_vectors.txt.
  artifact     required, repeatable, at least one. `name|size|sha256|url`:
                 name    destination relative to the install directory. Checked against the
                         payload-name rules -- no traversal, no absolute paths, no drive letters,
                         no alternate data streams, no reserved device names, no duplicates, ASCII
                         only. Refused, never sanitised.
                 size    bytes, non-zero, within the per-file and total limits.
                 sha256  exactly 64 LOWERCASE hex characters. Uppercase is rejected rather than
                         folded, so there is one spelling and the signature covers it unambiguously.
                 url     https only, no credentials. Enforced here, where the manifest is read, so
                         a caller that downloads some other way cannot sidestep it.
               Order is preserved and is part of the contract: the updater replaces files in this
               order and a rollback restores from the backups made along the way.
  versionCode  android only. Integer; Android refuses an update whose versionCode is not higher.

Limits (each runtime enforces the same ones): at most 64 artifacts, 512 MB per file, 2 GB total.
A signed manifest is still a manifest someone could have made a mistake in.

Signature
---------

Detached: the signature is NOT part of the document, so the bytes that were signed are exactly the
bytes on disk. One signature covers the whole document -- release identity, platform, architecture,
version and the entire file list -- so a name cannot be paired with a different hash, and a hash
cannot be paired with a different URL, without the signature failing.

  * ECDSA P-256 with SHA-256.
  * Raw r||s -- 64 bytes, IEEE P1363 form, NOT DER. Transported as 128 hex characters.
  * The public key is compiled into the product as raw X||Y (64 bytes, 128 hex characters), with
    no leading 0x04 tag.

Verification happens BEFORE the document is parsed, and that ordering is structural rather than
conventional: parsed fields are reachable only through `VerifiedManifest`, which nothing outside
`load_manifest()` can construct. A document that is both malformed and badly signed reports
SignatureInvalid, never Malformed -- which is how the tests tell the two steps have not been
reordered. Nothing may be downloaded or written on the strength of a name or URL that has not been
through that check.

This signature is NOT Authenticode. It says the artifact is one we published; it does nothing for
the UAC publisher prompt or for SmartScreen, which need a code-signing certificate.

Test vectors in this directory
------------------------------

  test_manifest.txt      a sample release with THREE artifacts of different sizes and contents,
                         one of which is GNLinkSetup.exe -- the installer travels in the package
                         it installs, so an update leaves the maintenance binary current
  test_manifest.sig      its signature, hex
  test_public_key.txt    the matching public key, hex X||Y
  files/                 the artifact bodies, so a test can stage real files whose hashes are the
                         ones the manifest claims

TEST DATA ONLY. Signed once with a throwaway P-256 key that was generated, used, and discarded --
it is not stored anywhere and it is not a release key. Regenerating them means generating a fresh
key and re-signing.

No release key is compiled into the product: `trusted_public_key_hex()` returns an empty string on
Windows and `trustedPublicKeyHex()` does the same on Android, which makes the default verifier
reject everything. That is deliberate and it is the safe direction -- generating, storing and
rotating a real signing key is a separate approved decision (design 4.5, "code ready" is not
"deployable"), and a placeholder that happened to verify something would be worse than no key.
