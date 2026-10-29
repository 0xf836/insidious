# Archive Fixtures

`images-rar4.rar` and `images-rar5.rar` are original synthetic test data distributed under the project's MIT license. No third-party image or archive content is copied.

Regenerate both byte-for-byte using `python3 tests/fixtures/generate_archives.py` from the repository root. The generator uses Python's standard library to assemble stored (uncompressed, unencrypted) RAR4/RAR5 headers and CRCs around two tiny generated PNGs and a text entry. Normal CTest execution needs neither Python nor proprietary RAR tools.

The RAR5 layout follows [RARLAB's format specification](https://www.rarlab.com/technote.htm); the RAR4 header fields correspond to the public UnRAR structures. These fixtures verify enumeration/filtering, a nested folder, bounded original-byte reads, sequential reads and pixel-correct display decode. They do not qualify compressed, solid, multivolume or encrypted RAR variants.
