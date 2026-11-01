# Changelog

All notable changes to this library. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). The library has no
release tags yet, so entries are dated.

## 2026-11-01

### Added

- **`bench/compare.cc`** (`CHAR_CLASSIFY_BUILD_BENCH`, off by default; abseil is found with `find_package` in the
  bench target only). Classification, hex encode and decode, case folding, scanning and the escaping kernel
  against arithmetic loops, `<cctype>`, `memchr`, a scalar table loop and abseil. Both sides compute the same
  result, checked before a row is timed; `--check` runs only the checks and is a ctest when the option is on.
  `char_classify_compare_swar` is the same program with `CHARS_NO_SIMD`, for a paired run of the vector backend
  against the portable one. The README Performance section has the numbers on an Apple M4 Pro and an Intel
  i9-9980HK, with the rows where the library is behind: a compiler-vectorized arithmetic loop is 1.3x faster than
  the `is_alpha` table, `FirstOf<1>` is 1.4x to 2.6x slower than the system `memchr` on x86 from 256 bytes up,
  `hexdec` takes twice as long as arithmetic, and `absl::HexStringToBytes` is 2x to 3.4x faster than `hex_value`.
- **`tools/mutants.py`.** 97 deliberate bugs in `chars.hh`, `chars_simd.hh` and `chars_escape.hh` (the hex table,
  the scalar rules and table of the escaper, the scalar pair and the shared-table lookup, the 8-byte test and its
  loops, the block kernel, the mask operations, the scanners, the case fold, and each backend's own operations),
  each built against the three tests in the target's backend and with `CHARS_NO_SIMD`; every one must make a test
  fail, crash or hang. On the i9 (SSE2 and portable) 91 of 91 are killed, on the M4 Pro (NEON and portable) 88 of
  88; a backend the host does not build is skipped and reported. Five changes that cannot alter any output are
  left out, among them the value of `escape_scalar_max`, which only chooses between two paths that write the same
  bytes.
- **`escape_scalar_max`, `escape_measure_scalar`, `escape_emit_scalar` and `escape_table_shared` in
  `chars_escape.hh`.** The byte loops that `escape_measure` and `escape_emit` already ran for a tail, named and
  usable on their own for any length, the length below which the 8-byte step does not start (8), and the shared
  table for `'\0'`, `'\''` and `'"'` (`nullptr` for any other quote, which needs a table built in a scratch). An
  entry point that sees fewer than `escape_scalar_max` bytes can escape them with these before it sets up anything
  for the block loops. `test/escape_kernel_test.cc` checks the pair against the oracle (exact room, and the output
  and input against an unmapped page) and the table lookup for every quote byte; `tools/mutants.py` has four
  mutants of them, all killed.
- **`test/guard_page.hh` and two guard-page tests.** `test_no_overread` in `test/simd_test.cc` runs `find`,
  `find_outside`, `ascii_lower` and `ascii_upper` with the input and the output ending exactly at an unmapped
  page, and the input starting exactly at one, for every length up to three blocks; `test_no_overrun` in
  `test/escape_kernel_test.cc` does the same for `escape_measure` and `escape_emit` with the output exactly the
  measured size, in four quote modes. A read or write one byte outside the bytes given is a crash in any build,
  where before it needed a sanitizer. The first run of `tools/mutants.py` had four survivors; two are equivalent
  mutants (an `if` that the loop condition repeats, and the quote slot of the writer, which a NUL does not change
  because it is outside the printable range), and two read one byte past the end in the tail block of
  `FirstOf::find` and `Outside::find` without changing the result. The guard-page test kills those two, and the
  mutants that read a byte beyond the input in the 8-byte loops of measure and emit die with a segmentation fault.

### Changed

- **`escape_measure` and `escape_emit` use the scalar pair for their tails.** The 8-byte step starts at
  `escape_scalar_max` bytes and below that both functions are the scalar loop, as before; the loop is now a
  function of its own that `repr` and `escape` call directly for a short input. The writer and the measurer
  still agree byte for byte: `test/escape_kernel_test.cc` checks the exact room, and the tests of `repr` and
  `escape` compare with their frozen originals.

### Measured

Against the frozen original of `repr` and `escape` (their changelogs have the tables), one to four printable
bytes were 0.4 to 3 ns slower with the shared kernel when the entry points set up the block loops before they
looked at the size. With the short path (entry points that handle fewer than 8 bytes with the scalar pair) no
row of 1 to 16 bytes is below 0.97x on the M4 Pro or the i9, and from 7 bytes up it is 2.5x to 3.7x faster.

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

### Measured

Through `escape()` and `repr()` (the tables are in their changelogs), against the previous
8-byte SWAR kernel each repository carried: 2.2x to 3.0x faster on 4 KB printable input,
2.1x (M4 Pro) and 3.7x (i9-9980HK) on 64 KB input with 10% non-printable bytes, and no row
more than 2% slower on either machine (inputs of 1 to 40 bytes are within noise).
`absl::CEscape`, which writes another format, takes 1.3x (i9, `repr`) to 2.8x (M4 Pro,
`repr`) longer on the 64 KB mixed row. GitHub runners on Apple silicon, Neoverse, AMD EPYC
and Intel Xeon agree: the same row is 2.9x to 6.9x faster than the previous kernel and 1.3x
to 3.1x faster than `absl::CEscape`.

The portable backend (`CHARS_NO_SIMD`, used only where neither NEON nor SSE2 exists, and by
the `*_swar` test twins) is not the one measured above. For `escape` it is slower than the
previous kernel on most inputs (0.9x on printable text on the M4 Pro; 0.7x to 0.9x on the
i9) and faster only on dense escapes on the M4 Pro (1.1x). For `repr` it is level on the M4
Pro and 0.85x to 1.0x on the i9.

### Changed

- Documentation: the README, `docs/API.md`, `ARCHITECTURE.md` and `AGENTS.md` describe the two new headers.
