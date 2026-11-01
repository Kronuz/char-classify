# Architecture

The internal design of `char-classify`: a single header holding table-driven,
`constexpr` character classification. For usage see `README.md`; for the repo map
and invariants see `AGENTS.md`.

## Shape

Three headers, header-only, zero dependencies beyond the C++ standard library:

```
  chars.hh         a 256-entry classification table + constexpr accessors, the hex tables
  chars_simd.hh    block scanners: first-of, outside-a-range, ASCII case fold (NEON, SSE2, SWAR)
  chars_escape.hh  the C escaping kernel: length table, measuring pass, writing pass
```

`chars_escape.hh` includes the other two; `chars_simd.hh` includes nothing of the library.

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

## Hex values as bytes

`hexdigit` keeps the nibble in the low 16 bits of a 32-bit flag word and answers
`0xffffffff` for a non-digit. `hex_value_table` is the same function as 256 bytes
with a `0xff` sentinel, built at compile time from the three digit ranges: a decoder
that holds its state in bytes (a percent-escape decoder) reads one byte per input
byte and compares against `0xff`. The two agree on every byte (`test/test.cc`).

## Block scanners

`chars_simd.hh` answers three questions about a block of `width` bytes: which are in
a set (`FirstOf<N>`), which are outside a range or in a set (`Outside<N>`), and what
the block is with ASCII case folded (`ascii_lower`, `ascii_upper`). Each backend
supplies the same few operations (`load`, `store`, `splat`, `eq`, `outside`,
`either`, `to_mask`, `lower`, `upper`) and the classes are written once over them.

- **NEON** compares 16 lanes (`vceqq_u8`; a range is one unsigned subtract and one
  compare) and narrows the result with `vshrn` to a 64-bit mask of one bit per
  nibble.
- **SSE2** compares 16 lanes and takes `movemask`. SSE2 has no unsigned byte
  compare, so a range is one add of `0x80 - lo` and one signed greater-than.
- **SWAR** holds 8 lanes in a `uint64_t`. A lane equals a value exactly when
  `((z & 0x7f..) + 0x7f..) | z` has no high bit, which carries nothing into the
  next lane, so the mask has no false positives above a true one (a plain
  `(z - 0x01..) & ~z & 0x80..` zero-byte test does). A range works on the low seven
  bits of each lane plus the lane's own high bit, so it is limited to `hi <= 0x7f`;
  the other backends take the same limit so the three never disagree.

A `Mask` has one flagged bit per lane, spread by the backend (`mask_shift` bits per
lane), and is read through `first`, `drop_first` and `count`. `find` runs whole
blocks and finishes with one block that overlaps the bytes already scanned, so it
reads only `[p, end)` and needs no scalar tail above one block. Inputs shorter than
a block go byte by byte.

## The escaping kernel

`chars_escape.hh` writes a byte as itself, as a two-byte escape (`\n`, `\r`, `\t`,
a backslash before a backslash or the quote) or as `\xNN`. An `EscapeTable` per
quote mode holds each byte's output length, its second byte, and its output bytes
(`out[c]`, zero padded to four). The same predicate, "below `0x20`, from `0x7f`, a
backslash or the quote", is what the table encodes and what the vector backends test,
and `test/escape_kernel_test.cc` checks the two against each other for every byte in
every quote mode.

`escape_measure` counts the output in one pass. A vector backend tests a 16-byte block for
"needs an escape" and adds 16 for a clean block, the common one; for a block with escapes it
adds 16 plus three for each flagged byte and subtracts two for each that is a two-byte escape
(`16 + 3 * dirty - 2 * pair`, from the popcounts of two masks). `escape_emit` writes the
result with no bounds check, so the caller's room must be the measured size. A vector
backend tests a block once and writes it one of three ways:

- clean: one 16-byte store;
- up to five escapes: the escapes are walked from the mask. The bytes before each one are
  copied with a whole-block store at the first byte not yet written, and the output advances
  only past the real ones; the escape itself is its four table bytes in one store, advancing
  by its length. Each store overshoots and the next one overwrites the overshoot;
- six or more escapes: every byte of the block goes through its table entry (a plain byte's
  entry is the byte itself), because the runs between escapes are too short to pay for a
  store each.

The stores stay inside the measured room because every input byte takes at least one output
byte: the loop runs while at least two blocks (32 bytes) remain, so a block store at any
byte of the current block has 16 input bytes behind it, and the table store after an escape
has 2 for the escape and at least 16 more. The last 31 or fewer bytes go through the 8-byte
SWAR test and then byte by byte through `escape_byte`, which writes `\xNN` with
`char_repr`. The portable backend is that 8-byte loop alone.

The two block loops are out of line (`CHARS_NOINLINE`) and the thin functions around them
are forced inline (`CHARS_ALWAYS_INLINE`), so an input under 16 bytes runs the same few
instructions as a loop that never heard of vectors: a call and a prologue cost as much as
the whole of a one-byte escape.

That holds for the kernel; it also has to hold for the function that calls it. An entry
point that contains the inlined block code, a table scratch for a custom quote and a string
to fill saves every register those need and reserves their frame before it looks at the
size, and for one to four bytes that costs more than the escaping itself. So the kernel names the short case:
`escape_scalar_max` (8, where the 8-byte step starts), `escape_measure_scalar` and
`escape_emit_scalar` (the byte loops, for any length), and `escape_table_shared(quote)` (the
shared table for `'\0'`, `'\''` or `'"'`, or `nullptr` for a quote that needs a scratch). An
entry point checks the size first, handles a short range with those four in its own few
instructions, and sends the rest to a `CHARS_NOINLINE` function that holds the scratch and
the block loops; `repr` and `escape` are written that way.
