# Changelog

All notable changes to this library. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). The library has no
release tags yet, so entries are dated.

## 2026-10-31

### Added

- **`chars_simd.hh`: block scanners over NEON, SSE2 or a 64-bit SWAR fallback.**
  `chars::simd::FirstOf<N>` (the first byte equal to any of N values),
  `chars::simd::Outside<N>` (the first byte outside an ASCII range or equal to any of N
  values), `find_outside`, and `ascii_lower` / `ascii_upper`. The three backends give the
  same results, which `test/simd_test.cc` checks against a scalar reference for every byte
  at every lane, every short length and with guard bytes around the buffer. The SWAR
  zero-byte test is exact, so a mask never has a false positive above a true one.
  `CHARS_NO_SIMD` forces the portable backend; the CTest targets `chars_simd_swar` and
  `chars_escape_kernel_swar` run the same tests with it.
- **`chars_escape.hh`: the C escaping kernel `escape()` and `repr()` share.**
  `chars::EscapeTable`, `chars::escape_table(quote, scratch)`, `chars::escape_measure` and
  `chars::escape_emit`. It replaces the copy of the same 100 lines in each of the two
  repositories. `test/escape_kernel_test.cc` checks it against an oracle written from the
  rules: every byte at every position of every length up to 70, random mixes at seven
  escape densities, nine quote modes (including bytes the table does not special-case),
  and the room the writer touches (an exact-size buffer for sanitizers, slack canaries
  otherwise).
- **`chars::hex_value(c)` and `chars::hex_value_table`: the byte-wide hex digit table**,
  `0xff` (`chars::NO_HEX_VALUE`) for any byte that is not a hex digit. It agrees with
  `hexdigit` on all 256 bytes.

### Changed

- Documentation: the README, `docs/API.md`, `ARCHITECTURE.md` and `AGENTS.md` describe the two new headers.
