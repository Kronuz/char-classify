# char-classify

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/char-classify/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/char-classify/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Header-only](https://img.shields.io/badge/header--only-yes-green.svg)

Character classification for C++20 without `<cctype>` or locales: `is_digit`,
`is_alpha`, `toupper`, hex digits, and more, as `constexpr` table lookups you can
use at compile time. For parsers and tokenizers that need fast, predictable,
locale-independent answers.

![char-classify: the demo's output](docs/char-classify.svg)

## ✨ Highlights

- **`constexpr` everything**: classify and convert in `static_assert`.
- **No locale, no `<cctype>`**: the same answer everywhere, with no global state.
- **One table lookup per question**: a 256-entry flag table, plus case and hex
  tables.
- **Hex helpers**: `hexdigit`, `hexdec` (two characters to a byte), `hex_value`
  (a byte-wide nibble table with a `0xff` sentinel), and `char_repr` (a byte to
  two hex characters).
- **Block scanning** (`chars_simd.hh`): find the first byte in a set, or outside
  a range, and fold ASCII case, 16 bytes at a time with NEON or SSE2 and 8 with a
  portable fallback.
- **The escaping kernel** (`chars_escape.hh`): the measure and write passes
  behind `escape()` and `repr()`.
- **Headers only, no dependencies.**

## 📦 Installation

Requires a C++20 compiler; CMake 3.14 or newer for the CMake route. CI builds it
on Linux (x86_64, arm64), macOS (arm64, x86_64), and FreeBSD, plus ASan, UBSan,
and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(char_classify
  GIT_REPOSITORY https://github.com/Kronuz/char-classify.git
  GIT_TAG        main)
FetchContent_MakeAvailable(char_classify)

target_link_libraries(my_app PRIVATE char_classify::char_classify)
```

Or copy the headers onto your include path: `chars.hh`, and `chars_simd.hh` and
`chars_escape.hh` when you use them (`chars_escape.hh` includes the other two).

## 🚀 Quick start

```cpp
#include <string_view>

#include "chars.hh"
#include "chars_simd.hh"

static_assert(chars::is_digit('5'));
static_assert(chars::toupper('a') == 'A');
static_assert(chars::hexdigit('f') == 15);
static_assert(chars::hex_value('g') == chars::NO_HEX_VALUE);

int main() {
	const char* p = "ff";
	chars::hexdec(&p);                           // decodes two hex digits, advancing p

	std::string_view s = "a=1&b=2";
	chars::simd::FirstOf<2> sep('&', '=');
	const char* hit = sep.find(s.data(), s.data() + s.size());   // the '=' after "a"
	const char* bad = chars::simd::find_outside(s.data(), s.data() + s.size(), '0', '9');
	return hit - s.data() == 1 && bad == s.data() ? 0 : 1;
}
```

The `is_*` predicates return a masked flag, not a `bool`; compare to `0` when you
need one. [`examples/demo.cc`](examples/demo.cc) classifies a sample string.

Define `CHARS_NO_SIMD` before including `chars_simd.hh` to force the portable
backend; `chars::simd::backend` names the one compiled in (`"neon"`, `"sse2"` or
`"swar"`).

## 🎯 When to use it

- ✅ Lexers, parsers, and encoders that classify bytes in hot loops.
- ✅ Scanning a buffer for a few delimiters, or for the first byte outside a class.
- ❌ Unicode: it classifies single bytes.

## 📚 Documentation

- [docs/API.md](docs/API.md): every predicate, conversion, scanner and the escaping kernel.
- [ARCHITECTURE.md](ARCHITECTURE.md): the tables and the block scanners.
- [CHANGELOG.md](CHANGELOG.md): what changed, with numbers.
- [AGENTS.md](AGENTS.md): repository map and traps.

## 🛠️ Building and testing

```sh
cmake -B build && cmake --build build && ctest --test-dir build
./build/char_classify_demo
```

## 🤝 Contributing

Issues and pull requests are welcome. Read [AGENTS.md](AGENTS.md) first. CI must
pass on every platform.

## 📄 License

MIT, Copyright (c) 2015-2019 Dubalu LLC. See [LICENSE](LICENSE). Extracted from
[Xapiand](https://github.com/Kronuz/Xapiand).
