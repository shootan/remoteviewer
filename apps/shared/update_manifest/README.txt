Update manifest format
======================

What the server publishes to say an update exists, and the only thing that decides whether a
machine is behind. Design: docs/업데이트_기능_설계.md sections 4.1-4.4.

The document
------------

Line-oriented `key=value` UTF-8 text. Not JSON, for two reasons:

  * The signature covers the document's exact bytes. A format with no canonicalisation step has
    nothing to get wrong between the signer and the verifier -- no key ordering, no whitespace
    rules, no number formatting.
  * The same document has to be read by C++, JavaScript and Kotlin without dragging a parser
    dependency into any of them.

Parsing rules:

  * Blank lines and lines whose first non-space character is `#` are ignored.
  * Leading and trailing spaces and tabs are stripped from both key and value; a trailing CR is
    stripped, so a document that travelled as CRLF still parses.
  * A line with no `=` is an error.
  * Unknown keys are IGNORED, not rejected -- a newer server adding a field must not brick an
    older client. Incompatible changes are gated by `schema` instead.

Fields
------

  schema       required. Format version of this document. Only 1 is understood today; anything
               else is reported as UnsupportedSchema rather than guessed at.
  platform     required. `windows` or `android`. A manifest for another platform is reported as
               WrongPlatform, which is not an error -- just not for us.
  version      required. Product version, compared by the shared contract in
               apps/shared/version_compare_vectors.txt.
  artifact     required. Name of the file to download.
  size         required, non-zero. Bytes. The download stage checks against this.
  sha256       required. Exactly 64 LOWERCASE hex characters. Uppercase is rejected rather than
               folded, so there is one spelling and the signature covers it unambiguously.
  versionCode  android only. Integer; Android refuses an update whose versionCode is not higher.

Signature
---------

Detached: the signature is NOT part of the document, so the bytes that were signed are exactly
the bytes on disk.

  * ECDSA P-256 with SHA-256.
  * Raw r||s -- 64 bytes, IEEE P1363 form, NOT DER. Transported as 128 hex characters.
  * The public key is compiled into the product as raw X||Y (64 bytes, 128 hex characters), with
    no leading 0x04 tag.

Verification happens BEFORE the document is parsed, and that ordering is structural rather than
conventional: parsed fields are reachable only through `VerifiedManifest`, which nothing outside
`load_manifest()` can construct. A document that is both malformed and badly signed reports
SignatureInvalid, never Malformed -- which is how the test tells the two steps have not been
reordered.

This signature is NOT Authenticode. It says the artifact is one we published; it does nothing for
the UAC publisher prompt or for SmartScreen, which need a code-signing certificate.

Test vectors in this directory
------------------------------

  test_manifest.txt      a sample document
  test_manifest.sig      its signature, hex
  test_public_key.txt    the matching public key, hex X||Y

TEST DATA ONLY. These were signed once with a throwaway P-256 key that was generated, used, and
discarded -- it is not stored anywhere and it is not a release key. Regenerating them means
generating a fresh key and re-signing.

No release key is compiled into the product: `trusted_public_key_hex()` returns an empty string,
which makes the default verifier reject everything. That is deliberate and it is the safe
direction -- generating, storing and rotating a real signing key is a separate approved decision
(design 4.5, "code ready" is not "deployable"), and a placeholder that happened to verify
something would be worse than no key at all.
