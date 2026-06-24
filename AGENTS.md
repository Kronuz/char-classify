# AGENTS.md

Working notes for agents modifying this repository. For the design read
`ARCHITECTURE.md`; for usage read `README.md`. This file covers the repo layout,
how to build and test, the invariants you must not break, and the traps that are
easy to fall into.

## Repo map

```
chars.hh                     Table-driven constexpr char classification (is_*, hexdigit/hexdec, tolower/toupper, char_repr). Header. Verbatim from Xapiand.
test/test.cc                 Runnable smoke test: classification, hex helpers, case folding.
CMakeLists.txt               INTERFACE library `char_classify` (+ alias char_classify::char_classify); CTest test `char_classify`.
LICENSE                      MIT, Copyright (c) 2015-2019 Dubalu LLC.
README.md                    What it is, install, usage.
ARCHITECTURE.md              Internal design of the classification table, trade-offs.
```

Everything is header-only. There is no `.cc` to compile except the test. The
CMake target is a pure `INTERFACE` library that only adds the source dir to the
include path and requests `cxx_std_20`.

## Build and run the test

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

Expected output ends with `all char-classify tests passed`, exit 0. The test
target is `char_classify_test`; the registered CTest name is `char_classify`.

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
  it with `static_assert`. Don't introduce a runtime-only path.
- **`is_*` returns an `int`, not a `bool`.** The predicates return the masked
  flag bits, so a true result is some non-zero `int`, not necessarily `1`. This
  is Xapiand's original contract; callers compare to `0` when they need a strict
  boolean. Don't "clean it up" to `bool` — it would change the contract and the
  packed-flag idiom the table relies on.
- **`hexdigit` returns the nibble value, `hexdec` decodes a pair.** `hexdigit('a')
  == 10`; `hexdec(&p)` reads two hex chars, returns the byte, and advances `p`
  only if the result is in range. Keep both.

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
already, so it was copied verbatim — there is no decoupling delta. Keep it that
way; any change here should be reconcilable with upstream as a plain edit.
