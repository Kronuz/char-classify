# Architecture

The internal design of `char-classify`: a single header holding table-driven,
`constexpr` character classification. For usage see `README.md`; for the repo map
and invariants see `AGENTS.md`.

## Shape

One header, header-only, zero dependencies beyond the C++ standard library:

```
  chars.hh   a 256-entry classification table + constexpr accessors
```

## The classification table

One `constexpr unsigned int char_tab[256]` packs a bitset of flags per byte:
`IS_SPACE`, `IS_ALPHA`, `IS_UPPER`, `IS_DIGIT`, `IS_HEX_DIGIT`, `IS_ASCII`,
`IS_KEYWORD`, and `IS_NON_HEX`, plus the low 16 bits carrying the hex nibble value
for hex digits. Each `is_*(char)` accessor is a `constexpr` masked lookup —
`char_tab[(unsigned char)c] & IS_DIGIT`, and so on. Because the predicates return
the masked flag bits, a true result is some non-zero `int`, not necessarily `1`;
that is Xapiand's original contract, preserved here.

`hexdigit` returns the packed nibble value (`hexdigit('a') == 10`); `hexdec`
decodes two hex chars to a byte and advances the read pointer only when the result
is in range. `is_keyword` flags the byte set Xapiand treats as keyword characters
(`-.:_0-9a-zA-Z(/\@<>=*[{"`).

## Case folding and hex repr

Separate 256-entry tables drive `tolower` / `toupper` — full-byte case fold, no
locale — and `char_repr`, which writes the two-char lowercase hex of a byte
through a `char**` write pointer. Like the classifiers, these are `constexpr`, so
case folding and hex rendering can happen at compile time.

## Why this shape

The whole reason this header exists is to do character classification without
`<cctype>`. `<cctype>` is locale-dependent and not `constexpr`, so it can't be
used in a `static_assert` or fold away at compile time. A single packed table and
a handful of masked lookups give the same answers, deterministically, in
`constexpr` context, with one cache-friendly table instead of a function call into
the C library. It is ASCII-oriented by design: the high half of the table carries
few flags, because the call sites that use it (lexing, hex decode, keyword
scanning) are ASCII. That focus is what keeps it small and fast.
