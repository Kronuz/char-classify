# char-classify performance

`bench/compare.cc` times the library against arithmetic loops, `<cctype>`, `memchr`, a scalar
loop over the escape table and abseil. Both sides of a row compute the same result, checked
before the row is timed; `--check` runs only the checks, and is a ctest when the option is on.
A time is the best of seven samples of at least 8 ms.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCHAR_CLASSIFY_BUILD_BENCH=ON
cmake --build build
./build/char_classify_compare          # the target's backend; add a word to filter rows
./build/char_classify_compare_swar     # the same rows on the portable backend
```

`-O3`, Apple clang 17, abseil 20260817.0, on an Apple M4 Pro (NEON) and an Intel Core
i9-9980HK (SSE2). The i9 is shared and moves by up to 25% between runs; the ratio inside a run
is the evidence. Each cell is library time / comparator time.

## ns per byte

| workload | M4 Pro | i9-9980HK |
|---|---:|---:|
| `is_alpha` count, 64 KB, against `(c \| 32) - 'a' < 26` (vectorized by the compiler) | 0.178 / 0.141 | 0.266 / 0.199 |
| `is_alpha` count, 64 KB, against `std::isalpha` | 0.178 / 0.433 | 0.263 / 1.107 |
| `is_alpha` count, 64 KB, against `absl::ascii_isalpha` | 0.175 / 0.176 | 0.265 / 0.266 |
| `is_hexdigit` count, 64 KB, against range arithmetic | 0.177 / 0.355 | 0.263 / 0.550 |
| `char_repr` hex encode, 256 B, against a nibble string lookup | 0.287 / 0.342 | 0.386 / 0.626 |
| `char_repr` hex encode, 256 B, against `absl::BytesToHexString` | 0.287 / 0.390 | 0.386 / 0.780 |
| `hexdec` decode, 4 KB, against arithmetic nibbles | 2.284 / 1.056 | 4.137 / 2.020 |
| `hex_value` decode, 4 KB, against arithmetic nibbles | 1.205 / 1.048 | 2.107 / 2.078 |
| `hex_value` decode, 4 KB, against `absl::HexStringToBytes` | 1.216 / 0.356 | 2.100 / 1.019 |
| `simd::ascii_lower`, 64 KB, against a `__restrict` arithmetic loop | 0.017 / 0.017 | 0.038 / 0.038 |
| `simd::ascii_lower`, 64 KB, against `chars::tolower` per byte | 0.017 / 0.194 | 0.037 / 0.372 |
| `simd::ascii_lower`, 16 B, against the arithmetic loop | 0.042 / 0.459 | 0.162 / 0.304 |
| `escape_measure` + `escape_emit`, 4 KB printable, against a two-pass table loop | 0.071 / 0.516 | 0.144 / 0.843 |
| the same, 4 KB text with 5% escapes | 0.157 / 0.528 | 0.351 / 0.842 |
| the same, 64 KB with one byte in ten random | 0.217 / 0.531 | 0.744 / 0.845 |

## ns per call

| workload | M4 Pro | i9-9980HK |
|---|---:|---:|
| `FirstOf<2>`, 256 B, no match, against `string_view::find_first_of` | 5.1 / 86 | 12.1 / 106 |
| `FirstOf<2>`, 4 KB, no match, against `string_view::find_first_of` | 94 / 1,075 | 185 / 1,484 |
| `find_outside('0', '9')`, 4 KB, against `std::find_if` | 83 / 1,095 | 129 / 1,384 |
| `FirstOf<1>`, 256 B, no match, against `memchr` | 4.7 / 6.0 | 8.9 / 6.2 |
| `FirstOf<1>`, 4 KB, no match, against `memchr` | 81 / 85 | 128 / 49 |

## Where the library is not ahead

- A compiler-vectorized arithmetic loop beats the table lookup of `is_alpha` by 1.3x, and
  matches `simd::ascii_lower` at 256 B and above.
- `FirstOf<1>` runs at `memchr`'s speed on NEON and 1.4x to 2.6x slower than the system
  `memchr` on x86 from 256 B up. One byte is a job for `memchr`.
- `hexdec` (two digits, `-1` on bad input) takes twice as long as arithmetic. `hex_value`
  removes the difference, and `absl::HexStringToBytes` is faster than both.

## The portable backend

`char_classify_compare_swar` is built with `CHARS_NO_SIMD` and shows what the vector backends
buy. Portable time over native time:

| row | M4 Pro | i9-9980HK |
|---|---:|---:|
| escaping kernel, 4 KB printable | 2.0x | 3.4x |
| escaping kernel, 64 KB mixed | 2.4x | 3.6x |
| `FirstOf<2>`, 4 KB | 2.9x | 3.3x |
| `ascii_lower`, 64 KB | 1.9x | 1.4x |

On the portable backend the 64 KB mixed kernel row takes 1.0x (M4 Pro) and 3.1x (i9) the time
of the scalar table loop.
