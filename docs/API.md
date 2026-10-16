# char-classify API reference

## What it is

One header, `chars.hh`: table-driven, `constexpr` character classification with
no `<cctype>` and no locale. One 256-entry table packs the flags
(`is_digit`, `is_alpha`, `is_upper`, `is_space`, `is_hexdigit`, `is_ascii`,
`is_keyword`, ...); separate tables drive `tolower` / `toupper` and the
byte-to-hex `char_repr`. Every accessor is a `constexpr` masked lookup, so you
can classify characters at compile time. Nothing behind it but the C++ standard
library.

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
