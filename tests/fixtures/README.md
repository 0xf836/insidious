# Archive Fixtures

`images-rar4.rar` and `images-rar5.rar` are original synthetic test data distributed under the project's MIT license. No third-party image or archive content is copied.

Regenerate both byte-for-byte using `python3 tests/fixtures/generate_archives.py` from the repository root. The generator uses Python's standard library to assemble stored (uncompressed, unencrypted) RAR4/RAR5 headers and CRCs around two tiny generated PNGs and a text entry. Normal CTest execution needs neither Python nor proprietary RAR tools.

The RAR5 layout follows [RARLAB's format specification](https://www.rarlab.com/technote.htm); the RAR4 header fields correspond to the public UnRAR structures. These fixtures verify enumeration/filtering, a nested folder, bounded original-byte reads, sequential reads and pixel-correct display decode. They do not qualify compressed, solid, multivolume or encrypted RAR variants.

`encrypted-data-rar5.rar`, `encrypted-headers-rar5.rar`, `encrypted-data.7z`, and `encrypted-headers.7z` are original synthetic fixtures under the same MIT license. Each contains `sample.png`, a 3-by-2 RGB PNG filled with `(20, 120, 200)`, and uses the public test password `secret`. They cover encrypted content with visible names and encrypted headers. RAR5 tests verify unsupported-encryption errors, not decryption support. The 7z tests verify either that same error or successful decryption and password handling on a capable backend.

To regenerate the encrypted fixtures, create that PNG (the `png()` helper in `generate_archives.py` can generate it), then run these commands in a temporary directory containing only `sample.png`:

```sh
rar a -ma5 -psecret encrypted-data-rar5.rar sample.png
rar a -ma5 -hpsecret encrypted-headers-rar5.rar sample.png
7z a -t7z -psecret encrypted-data.7z sample.png
7z a -t7z -psecret -mhe=on encrypted-headers.7z sample.png
```

Regeneration requires RAR and 7-Zip and produces fresh random encryption salts; normal test execution uses the committed fixtures and needs neither tool.
