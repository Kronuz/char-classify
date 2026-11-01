# AGENTS.md

Working notes for agents modifying this repository. For the design read
`ARCHITECTURE.md`; for usage read `README.md`. This file covers the repo layout,
how to build and test, the invariants you must not break, and the traps that are
easy to fall into.

## Repo map

```
chars.hh                     Table-driven constexpr char classification (is_*, hexdigit/hexdec, hex_value, tolower/toupper, char_repr). Header. From Xapiand; the hex byte table is additive.
chars_simd.hh                Block scanners (FirstOf, Outside, find_outside, ascii_lower/upper) over NEON, SSE2 or a 64-bit SWAR fallback. CHARS_NO_SIMD forces the fallback. Header.
chars_escape.hh              The C escaping kernel shared by escape() and repr(): EscapeTable, escape_measure, escape_emit. Header.
test/test.cc                 Runnable smoke test: classification, hex helpers, case folding.
test/simd_test.cc            chars_simd.hh against scalar references (every byte at every lane, every short length, guard bytes, and the input and output against an unmapped page). Built twice.
test/escape_kernel_test.cc   chars_escape.hh against a scalar oracle (every byte at every position, random mixes, room checks, and the output and input against an unmapped page). Built twice.
test/guard_page.hh           GuardedRegion: a buffer that ends (or starts) exactly at an unmapped page, so a read or write one byte outside it crashes. POSIX; the guard tests compile to nothing elsewhere.
tools/mutants.py             Breaks the hex table, the block scanners and the escaping kernel in about 90 ways (per backend) and requires one of the tests to fail on each.
bench/compare.cc             Classification, hex, case folding, scanning and the escape kernel against arithmetic loops, <cctype>, memchr and, when found, abseil (CHAR_CLASSIFY_BUILD_BENCH). Also built with CHARS_NO_SIMD as a twin; `--check` is a CTest.
CMakeLists.txt               INTERFACE library `char_classify` (+ alias char_classify::char_classify); CTest tests `char_classify`, `chars_simd`, `chars_simd_swar`, `chars_escape_kernel`, `chars_escape_kernel_swar`, and with the bench option `char_classify_compare`, `char_classify_compare_swar`.
LICENSE                      MIT, Copyright (c) 2015-2019 Dubalu LLC.
CHANGELOG.md                 Dated changes, with numbers.
README.md                    What it is, install, quick start.
docs/API.md                  What it provides and usage.
ARCHITECTURE.md              Internal design of the classification table, the block scanners and the escaping kernel, trade-offs.
```

Everything is header-only. There is no `.cc` to compile except the tests. The
CMake target is a pure `INTERFACE` library that only adds the source dir to the
include path and requests `cxx_std_20`.

## Build and run the test

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

Expected output ends with `all char-classify tests passed`, exit 0. The test
target is `char_classify_test`; the registered CTest name is `char_classify`. The
scanner and kernel tests run twice, once with the backend of the target and once with
`CHARS_NO_SIMD` (the `_swar` tests), so both must agree with the reference.

After a change to `chars_simd.hh`, `chars_escape.hh` or the hex table, run
`python3 tools/mutants.py . [CXX]`: every mutant must be killed. A neon mutant needs an
arm64 host, an sse2 one an x86-64 host or `CXXFLAGS="-arch x86_64"` on an Apple silicon
Mac (Rosetta); the tool lists the mutants it skips. `-DCHAR_CLASSIFY_BUILD_BENCH=ON` (with
`-DCMAKE_BUILD_TYPE=Release`) builds `char_classify_compare` and `char_classify_compare_swar`,
the same program on the portable backend, for a paired run.

## Conventions

- **C++20.** The target requests `cxx_std_20` to stay uniform with the sibling
  libraries; the header itself is older-standard-friendly, but don't drop the
  target below it.
- **Zero external dependencies.** The only includes are the C++ standard library.
  Do not reach for `<cctype>` — it is locale-dependent and not `constexpr`, which
  is exactly what this header exists to avoid. There is no optional seam here.
- **Filename is stable.** The header keeps its original Xapiand name (`chars.hh`)
  so a consumer that already `#include`s it just needs this repo on the include
  path. Don't rename it.
- Tabs for indentation, double quotes in code, no em dashes in prose.

## Load-bearing invariants

- **The classification is table-driven and `constexpr`.** `char_tab[256]` packs a
  bitset of flags per byte; each `is_*(char)` is a masked lookup. `tolower` /
  `toupper` / `char_repr` are driven by their own 256-entry tables. All of it is
  `constexpr` so callers can classify at compile time — the test asserts most of
  it with `static_assert`. Don't introduce a runtime-only path in `chars.hh`
  (`chars_simd.hh` and `chars_escape.hh` work on buffers and are runtime by nature).
- **`is_*` returns an `int`, not a `bool`.** The predicates return the masked
  flag bits, so a true result is some non-zero `int`, not necessarily `1`. This
  is Xapiand's original contract; callers compare to `0` when they need a strict
  boolean. Don't "clean it up" to `bool` — it would change the contract and the
  packed-flag idiom the table relies on.
- **`hexdigit` returns the nibble value, `hexdec` decodes a pair.** `hexdigit('a')
  == 10`; `hexdec(&p)` reads two hex chars, returns the byte, and advances `p`
  only if the result is in range. Keep both. `hex_value` is the same nibble as a
  byte with `0xff` for a non-digit; the test holds it equal to `hexdigit` on all 256 bytes.
- **The three scanner backends give identical results.** A change to one backend's
  operation is a change to all three, and `test/simd_test.cc` (default and `_swar`)
  compares each with a scalar reference. A range takes `hi <= 0x7f` in every
  backend, because the SWAR test works on the low seven bits of a lane; do not
  loosen it in one.
- **The entry points handle a short range before they set anything up.** `escape_scalar_max`,
  `escape_measure_scalar`, `escape_emit_scalar` and `escape_table_shared` exist so that `repr()` and
  `escape()` can escape fewer than 8 bytes in a function with no table scratch and no block code: a
  frame that saves the registers of the block loops costs more than escaping one byte. Keep the block
  loops and the scratch out of line (`CHARS_NOINLINE`) in any new caller, and keep the two scalar
  functions free of anything that needs a frame. `escape_scalar_max` only chooses between two paths
  that write the same bytes, so `tools/mutants.py` has no mutant for it.
- **The kernel's table and its vector test describe one predicate.** A byte is plain
  when it is in `0x20..0x7e` and is neither a backslash nor the quote. If that set
  changes, change `make_escape_table`, `escape_byte`, the `Outside` test and the `pair`
  count in `escape_measure` together; `test/escape_kernel_test.cc` fails otherwise.
- **`escape_emit` stores past the byte it is writing, inside the measured room.** The
  block store needs 16 bytes and the table store 4; both are inside `escape_measure`
  bytes because the vector loop runs only while two blocks (32 bytes) remain. A change to
  the loop bound or to the store sizes needs that argument redone, and the test's
  exact-size buffer (a sanitizer report) and slack canaries check it.

## How to extend

- **Add a char predicate.** Add a flag bit to `char_tab` and a thin `constexpr`
  accessor that masks it; don't reach for `<cctype>`.
- **Always extend the smoke test.** `test/test.cc` is the only executable check.
  Prefer `static_assert` for the `constexpr` predicates so a regression is a
  compile error, not a silent runtime pass.

## Traps

- **The high half of the table (bytes 0x80–0xFF) is mostly `IS_NON_HEX` only.**
  Non-ASCII bytes carry few flags; don't assume a Latin-1 letter classifies as
  alpha. This header is ASCII-oriented by design.
- **`char_repr` writes lowercase hex and advances the write pointer by two.** It
  takes a `char**`; make sure the buffer has room for the two chars.

## Standalone vs. Xapiand

This is a standalone extraction from
[Xapiand](https://github.com/Kronuz/Xapiand). `chars.hh` had zero dependencies
already, so it was copied verbatim, and the one change to it (the `hex_value` byte
table) is an addition. `chars_simd.hh` and `chars_escape.hh` are new here. Keep any
change to `chars.hh` reconcilable with upstream as a plain edit.
