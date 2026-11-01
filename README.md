# char-classify

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/char-classify/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/char-classify/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Header-only](https://img.shields.io/badge/header--only-yes-green.svg)

Header-only character classification for C++20: locale-free constexpr table lookups and SIMD byte scanners.
Each predicate (`is_digit`, `is_alpha`, `is_hexdigit`, ...) and conversion (`toupper`,
`hexdigit`, `hex_value`) is a lookup in a 256-entry table, the same at compile time
and at run time, with no `<cctype>` and no locale. Two more headers scan whole blocks:
`chars_simd.hh` finds delimiters and folds case 16 bytes at a time, and
`chars_escape.hh` is the C escaping kernel behind `escape()` and `repr()`. Reach for it
in lexers, parsers and encoders that classify bytes in hot loops.

## ✨ Highlights

- **`constexpr` predicates and conversions.** Every `is_*` test, `tolower`, `toupper`,
  `hexdigit`, `hexdec` and `hex_value` works in a `static_assert`.
- **No locale, no `<cctype>`.** The same answer on every platform, with no global state.
- **One table load per question.** A 256-entry flag table, plus case and hex tables.
- **Hex helpers.** `hexdigit` (a nibble or `NO_HEXDIGIT`), `hexdec` (two characters to a
  byte, `-1` on bad input), `hex_value` (a byte-wide nibble table with a `0xff`
  sentinel) and `char_repr` (a byte to two hex characters).
- **Block scanning** (`chars_simd.hh`). `FirstOf<N>` finds the first byte in a set,
  `Outside<N>` and `find_outside` the first byte outside an ASCII range, and
  `ascii_lower` / `ascii_upper` fold case: 16 bytes per step with NEON or SSE2, 8 with
  the portable 64-bit backend.
- **The escaping kernel** (`chars_escape.hh`). `escape_measure` sizes the output and
  `escape_emit` writes exactly that many bytes, so there is no bounds check per byte.
- **Headers only, no dependencies.**

## 🚀 Quick start

```cpp
#include <cstdio>
#include <string_view>

#include "chars.hh"
#include "chars_simd.hh"

static_assert(chars::is_digit('5'));                 // classification runs in the compiler
static_assert(chars::toupper('a') == 'A');
static_assert(chars::hex_value('f') == 15);
static_assert(chars::hex_value('g') == chars::NO_HEX_VALUE);

int main() {
	const char* p = "ff";
	std::printf("%d\n", chars::hexdec(&p));          // 255, and p moves past both digits

	std::string_view s = "key=42&x";
	const char* end = s.data() + s.size();
	chars::simd::FirstOf<2> sep('&', '=');
	std::printf("%td\n", sep.find(s.data(), end) - s.data());                             // 3
	std::printf("%td\n", chars::simd::find_outside(s.data() + 4, end, '0', '9') - s.data());  // 6
	std::printf("%s\n", chars::simd::backend);       // neon, sse2 or swar
}
```

The `is_*` predicates return a masked flag (an `int`), not a `bool`; compare to `0`
when you need one. Define `CHARS_NO_SIMD` before including `chars_simd.hh` to force the
portable backend. [`examples/demo.cc`](examples/demo.cc) classifies a sample string and
tokenizes a line.

## 📦 Installation

Requires a C++20 compiler and CMake 3.14 or newer; no dependencies. CI builds it on
Linux (x86_64, arm64), macOS (arm64, x86_64), and FreeBSD, plus ASan, UBSan, and TSan
builds.

```cmake
include(FetchContent)
FetchContent_Declare(char_classify
  GIT_REPOSITORY https://github.com/Kronuz/char-classify.git
  GIT_TAG        684a962d113fc007be47df6f4b2c2771ea821d3f)  # or main for the tip
FetchContent_MakeAvailable(char_classify)

target_link_libraries(my_app PRIVATE char_classify::char_classify)
```

```cpp
#include "chars.hh"         // predicates, case, hex
#include "chars_simd.hh"    // block scanners
#include "chars_escape.hh"  // the escaping kernel (includes the other two)
```

Without CMake, copy `chars.hh` onto your include path, and `chars_simd.hh` and
`chars_escape.hh` when you use them.

## 🎯 When to use it

Use it for byte classification in lexers, parsers and encoders, and for scanning a
buffer for a few delimiters or for the first byte outside a class.

Not for:

- **Unicode.** It classifies single bytes, and a byte above `0x7f` is in no class.
- **A single delimiter.** `FirstOf<1>` runs at `memchr`'s speed on NEON and up to 2.6x
  slower than the system `memchr` on x86; use `memchr`.
- **Bulk hex decoding.** `absl::HexStringToBytes` decodes 4 KB 2x to 3.4x faster than a
  loop over `hex_value`.

## ⚡ Performance

`bench/compare.cc` times a workload twice in one run: the library's routine, and one comparator
that computes the same result, which the benchmark checks before it times the row. A time is ns
per byte, the best of seven samples of at least 8 ms; `× faster` is the comparator's time
divided by the library's, so a value below 1 means the library is slower. The comparators are
`std::isalpha` (libc++ 20.1), [abseil](https://github.com/abseil/abseil-cpp) 20260817.0
(`absl::BytesToHexString`, `absl::HexStringToBytes`), and loops written in the benchmark:
arithmetic on the byte, a per-byte `chars::tolower`, and a two-pass loop over the escape table.
`simd::ascii_lower` is the library's vector lowercase, and the escaping kernel is
`escape_measure` followed by `escape_emit`.

| Workload | Comparator | M4 Pro, library | M4 Pro, comparator | M4 Pro, × faster | i9-9980HK, library | i9-9980HK, comparator | i9-9980HK, × faster |
|---|---|--:|--:|--:|--:|--:|--:|
| `is_alpha` count, 64 KB | `std::isalpha` | 0.178 | 0.433 | 2.4 | 0.263 | 1.107 | 4.2 |
| `is_alpha` count, 64 KB | `(c \| 32) - 'a' < 26`, vectorized by the compiler | 0.178 | 0.141 | 0.8 | 0.266 | 0.199 | 0.7 |
| `char_repr` hex encode, 256 B | `absl::BytesToHexString` | 0.287 | 0.390 | 1.4 | 0.386 | 0.780 | 2.0 |
| `hex_value` decode, 4 KB | `absl::HexStringToBytes` | 1.216 | 0.356 | 0.3 | 2.100 | 1.019 | 0.5 |
| `simd::ascii_lower`, 64 KB | `chars::tolower` per byte | 0.017 | 0.194 | 11.4 | 0.037 | 0.372 | 10.1 |
| escaping kernel, 4 KB printable | a two-pass table loop | 0.071 | 0.516 | 7.3 | 0.144 | 0.843 | 5.9 |
| escaping kernel, 64 KB, one byte in ten random | the same loop | 0.217 | 0.531 | 2.4 | 0.744 | 0.845 | 1.1 |

Apple M4 Pro (NEON, macOS 26.7) and Intel Core i9-9980HK (SSE2, macOS 15.8), Apple clang
17.0.0 with `-O3`:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCHAR_CLASSIFY_BUILD_BENCH=ON
cmake --build build
./build/char_classify_compare
```

The i9 is shared and moves by up to 25% between runs; the ratio inside a run is the
evidence. [docs/perf/README.md](docs/perf/README.md) has every row, the scanners against
`memchr` and `find_first_of`, and the portable backend against the vector ones.

## 📚 Documentation

- [docs/API.md](docs/API.md): every predicate, conversion, scanner and the escaping kernel.
- [ARCHITECTURE.md](ARCHITECTURE.md): the tables, the block scanners and the kernel.
- [docs/perf/README.md](docs/perf/README.md): the full benchmark tables.
- [CHANGELOG.md](CHANGELOG.md): what changed.
- [AGENTS.md](AGENTS.md): repository map, invariants, and traps.
- [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#char-classify): the catalog entry.

## 🛠️ Building and testing

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

`./build/char_classify_demo` runs the demo. The `*_swar` tests run the scanner and kernel
tests again with `CHARS_NO_SIMD`. Add `-DCHAR_CLASSIFY_BUILD_BENCH=ON` to build
`char_classify_compare` and `char_classify_compare_swar`; with abseil installed they add
its rows.

## 🤝 Contributing

Issues and pull requests are welcome. Read [AGENTS.md](AGENTS.md) and the
catalog's [rules of engagement](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#rules-of-engagement)
first; CI must pass on every platform.

## 📄 License

MIT, Copyright (c) 2026 Germán Méndez Bravo and Copyright (c) 2015-2020 Dubalu
LLC. See [LICENSE](LICENSE). Extracted from [Xapiand](https://github.com/Kronuz/Xapiand).
