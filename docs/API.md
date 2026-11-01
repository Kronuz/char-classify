# char-classify API reference

## What it is

`chars.hh`: table-driven, `constexpr` character classification with
no `<cctype>` and no locale. One 256-entry table packs the flags
(`is_digit`, `is_alpha`, `is_upper`, `is_space`, `is_hexdigit`, `is_ascii`,
`is_keyword`, ...); separate tables drive `tolower` / `toupper` and the
byte-to-hex `char_repr`. Every accessor is a `constexpr` masked lookup, so you
can classify characters at compile time. Two more headers work on whole buffers:
`chars_simd.hh` scans a block of bytes at a time and `chars_escape.hh` is the C
escaping kernel. Nothing behind any of them but the C++ standard library.

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

## Hex values as bytes

`hex_value(c)` is the byte-wide sibling of `hexdigit`: a 256-byte table of nibble
values, `0xff` (`NO_HEX_VALUE`) for any byte that is not a hex digit. A decoder
that keeps its state in bytes reads `hex_value_table.v[c]` directly.

```cpp
static_assert(chars::hex_value('F') == 15);
static_assert(chars::hex_value('%') == chars::NO_HEX_VALUE);
```

## Block scanning: `chars_simd.hh`

Scanners that test a whole block of bytes at once. The block is 16 bytes with NEON
(AArch64) or SSE2 (x86-64) and 8 bytes in a 64-bit integer everywhere else; all
three give the same answers. `CHARS_NO_SIMD` (defined before the include) forces
the portable one, and `chars::simd::backend` is `"neon"`, `"sse2"` or `"swar"`.
`chars::simd::width` is the block size. The header needs nothing but the standard
library.

```cpp
#include "chars_simd.hh"

chars::simd::FirstOf<2> sep('&', '=');
const char* hit = sep.find(p, end);                        // first '&' or '=' in [p, end), or end

chars::simd::Outside<2> dirty(0x20, 0x7e, '\\', '\'');     // not printable, backslash or quote
const char* bad = dirty.find(p, end);

const char* sign = chars::simd::find_outside(p, end, '0', '9');   // first non-digit

chars::simd::ascii_lower(src, n, dst);                     // dst may equal src
chars::simd::ascii_upper(src, n, dst);
```

| name | meaning |
|---|---|
| `FirstOf<N>(c...)` | the bytes equal to any of `N` values |
| `Outside<N>(lo, hi, c...)` | the bytes outside `[lo, hi]` (`hi <= 0x7f`) or equal to any of `N` values; `N` may be 0 |
| `find_outside(p, end, lo, hi)` | the first byte outside `[lo, hi]` |
| `ascii_lower`, `ascii_upper` | fold `A-Z` or `a-z`; every other byte is copied |

`FirstOf` and `Outside` have `find(p, end)` (exact at any length, reads only
`[p, end)`), `contains(byte)`, and the block interface: `hits(p)` tests the
`width` bytes at `p` and returns a `Mask`, `first(p)` is the index of the first hit
in the block (`width` when none). The mask functions `first(m)`, `drop_first(m)`
and `count(m)` read a `Mask`; use them rather than its bits, which are spread
differently by each backend. `Vec`, `load`, `store` and `splat` give a block of
bytes to a caller that wants to store it as well (`chars_escape.hh` does).

## The escaping kernel: `chars_escape.hh`

The measure and write passes behind `escape()` and `repr()`. A byte is written as
itself, as `\n`, `\r`, `\t`, a backslash before a backslash or the quote, or as
`\xNN` (two lowercase hex digits) for bytes below `0x20` and from `0x7f`.

```cpp
#include "chars_escape.hh"

chars::EscapeTable custom;
const chars::EscapeTable& t = chars::escape_table('\'', custom);   // '\0', '\'' and '"' are shared
const auto* b = reinterpret_cast<const unsigned char*>(data);
const auto* e = b + size;

std::string out;
out.resize(chars::escape_measure(b, e, t));               // exact size, quotes not counted
chars::escape_emit(out.data(), b, e, t);                  // writes exactly that many bytes
```

`escape_emit` needs room for `escape_measure(b, e, t)` bytes at its destination
and writes nothing outside it, which is why a result sized from the measure needs
no bounds check per byte. `escape_byte(d, c, t)` writes one byte's escape.
`make_escape_table(quote)` is `constexpr`.
