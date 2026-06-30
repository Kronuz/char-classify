# char-classify

![constexpr](https://img.shields.io/badge/constexpr-compile--time-blue)

A small, header-only, dependency-free **compile-time character classification**
library for C++20, extracted from
[Xapiand](https://github.com/Kronuz/Xapiand).

## What it is

One header, `chars.hh`: table-driven, `constexpr` character classification with
no `<cctype>` and no locale. One 256-entry table packs the flags
(`is_digit`, `is_alpha`, `is_upper`, `is_space`, `is_hexdigit`, `is_ascii`,
`is_keyword`, ...); separate tables drive `tolower` / `toupper` and the
byte-to-hex `char_repr`. Every accessor is a `constexpr` masked lookup, so you
can classify characters at compile time. Nothing behind it but the C++ standard
library.

## Install

CMake with `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(
  char_classify
  GIT_REPOSITORY https://github.com/Kronuz/char-classify.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(char_classify)

target_link_libraries(your_target PRIVATE char_classify::char_classify)
```

The `char_classify` target is a pure `INTERFACE` library: it compiles nothing,
requests `cxx_std_20`, and puts the header directory on your include path. Then:

```cpp
#include "chars.hh"  // namespace chars
```

Requires C++20. On macOS it builds with AppleClang/libc++, the same toolchain
Xapiand uses. The header keeps its original filename, so a codebase that already
`#include "chars.hh"` just needs this repo on its include path.

## Usage

```cpp
#include "chars.hh"

static_assert(chars::is_digit('5'));
static_assert(chars::is_alpha('x'));
static_assert(chars::is_upper('X') && !chars::is_upper('x'));
static_assert(chars::is_hexdigit('f'));
static_assert(chars::hexdigit('a') == 10);    // the nibble value
static_assert(chars::tolower('A') == 'a');
static_assert(chars::toupper('a') == 'A');

// Decode two hex chars to a byte, advancing the pointer:
const char* p = "ff";
int b = chars::hexdec(&p);                     // 255
```

The `is_*` predicates return an `int` (a masked flag, not a clean `bool`), so
compare to `0` when you need a strict boolean. `hexdigit` returns the nibble value
of a hex digit; `hexdec` decodes two of them to a byte; `char_repr` writes the
two-char lowercase hex of a byte.

## Build & test

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

The test checks the classification predicates (digit/alpha/upper/space/alnum),
the hex helpers (`is_hexdigit`, `hexdigit`, `hexdec`), and case folding
(`tolower`/`toupper`, `char_repr`), almost all via `static_assert`. It prints
`all char-classify tests passed` and exits 0.

## Examples

[`examples/demo.cc`](examples/demo.cc) is a runnable tour. A top-level CMake build
produces it next to the test:

```sh
cmake -B build && cmake --build build && ./build/char_classify_demo
```

It classifies a row of characters across the predicates, proves with
`static_assert` that the classification (and case folding) ran in the compiler,
decodes hex bytes at compile time (including a rejected bad input), and then runs a
tiny tokenizer whose inner loop leans on `is_space` / `is_alnum` / `is_digit` to
split a line into words, numbers, and whitespace, with no `<cctype>` anywhere.

## Provenance

Extracted from [Xapiand](https://github.com/Kronuz/Xapiand). `chars.hh` had zero
dependencies already, so it was copied verbatim — no decoupling delta. See
[ARCHITECTURE.md](ARCHITECTURE.md) for the design and [AGENTS.md](AGENTS.md) for
the repo map and invariants.

This is one of the two foundation headers for the
[`hashes`](https://github.com/Kronuz/hashes) library, which uses
`chars::tolower` for its case-insensitive FNV.

## License

MIT, Copyright (c) 2015-2019 Dubalu LLC. See [LICENSE](LICENSE).
