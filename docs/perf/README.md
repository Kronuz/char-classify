# char-classify performance

`bench/compare.cc` times the library against arithmetic loops, `<cctype>`, `memchr`, a scalar
loop over the escape table and abseil. Both sides of a row compute the same result, checked
before the row is timed; `--check` runs only the checks, and is a ctest when the option is on.
A time is the best of seven samples of at least 8 ms, and a cell is the best of two runs.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCHAR_CLASSIFY_BUILD_BENCH=ON
cmake --build build
./build/char_classify_compare          # the target's backend; add a word to filter rows
./build/char_classify_compare_swar     # the same rows on the portable backend
```

`-O3`, Apple clang 17, abseil 20260817.0, on an Apple M4 Pro (NEON) and an Intel Core
i9-9980HK (SSE2). The i9 is shared and moves by up to 21% between runs; the ratio inside a run
is the evidence. Each cell is library time / comparator time.

## ns per byte

| workload | M4 Pro | i9-9980HK |
|---|---:|---:|
| is_alpha count, 64 KB, against (c \| 32) - 'a' < 26 | 0.177 / 0.142 | 0.262 / 0.193 |
| is_alpha count, 64 KB, against std::isalpha | 0.176 / 0.426 | 0.270 / 1.087 |
| is_hexdigit count, 64 KB, against arithmetic ranges | 0.176 / 0.358 | 0.269 / 0.556 |
| is_alpha count, 64 KB, against absl::ascii_isalpha | 0.175 / 0.174 | 0.272 / 0.266 |
| is_hexdigit count, 64 KB, against absl::ascii_isxdigit | 0.175 / 0.171 | 0.268 / 0.275 |
| char_repr hex-encode, 16 B, against nibble string lookup | 0.314 / 0.473 | 0.465 / 0.691 |
| char_repr hex-encode, 16 B, against absl::BytesToHexString | 0.311 / 1.319 | 0.469 / 3.425 |
| char_repr hex-encode, 256 B, against nibble string lookup | 0.285 / 0.344 | 0.380 / 0.628 |
| char_repr hex-encode, 256 B, against absl::BytesToHexString | 0.286 / 0.396 | 0.378 / 0.775 |
| char_repr hex-encode, 65536 B, against nibble string lookup | 0.260 / 0.328 | 0.377 / 0.595 |
| char_repr hex-encode, 65536 B, against absl::BytesToHexString | 0.257 / 0.260 | 0.376 / 0.362 |
| hexdec decode, 4096 B, against arithmetic nibbles | 2.269 / 1.062 | 4.171 / 2.879 |
| hex_value decode, 4096 B, against arithmetic nibbles | 1.231 / 1.054 | 1.969 / 2.900 |
| hex_value decode, 4096 B, against absl::HexStringToBytes | 1.221 / 0.352 | 1.932 / 0.954 |
| ascii_lower, 16 B, against arithmetic loop (__restrict) | 0.041 / 0.456 | 0.114 / 0.242 |
| ascii_lower, 16 B, against chars::tolower per byte | 0.041 / 0.278 | 0.116 / 0.428 |
| ascii_lower, 16 B, against absl::AsciiStrToLower (copy) | 0.041 / 0.390 | 0.115 / 0.792 |
| ascii_lower, 256 B, against arithmetic loop (__restrict) | 0.020 / 0.017 | 0.044 / 0.045 |
| ascii_lower, 256 B, against chars::tolower per byte | 0.020 / 0.214 | 0.044 / 0.372 |
| ascii_lower, 256 B, against absl::AsciiStrToLower (copy) | 0.021 / 0.350 | 0.044 / 0.693 |
| ascii_lower, 65536 B, against arithmetic loop (__restrict) | 0.017 / 0.017 | 0.038 / 0.039 |
| ascii_lower, 65536 B, against chars::tolower per byte | 0.018 / 0.191 | 0.038 / 0.374 |
| ascii_lower, 65536 B, against absl::AsciiStrToLower (copy) | 0.018 / 0.298 | 0.038 / 0.669 |
| escape_measure + escape_emit, printable, 4 KB, against scalar table loop, 2 passes | 0.071 / 0.524 | 0.143 / 0.842 |
| escape_measure + escape_emit, text with escapes, 4 KB, against scalar table loop, 2 passes | 0.157 / 0.520 | 0.346 / 0.851 |
| escape_measure + escape_emit, mixed 10%, 64 KB, against scalar table loop, 2 passes | 0.218 / 0.524 | 0.639 / 0.843 |

## ns per call

| workload | M4 Pro | i9-9980HK |
|---|---:|---:|
| FirstOf<1>, 16 B, no match, against memchr | 0.8 / 1.3 | 1.1 / 3.3 |
| FirstOf<2>, 16 B, no match, against string_view::find_first_of | 0.8 / 4.9 | 1.7 / 8.4 |
| FirstOf<2>, 16 B, no match, against byte loop | 0.8 / 5.0 | 1.7 / 8.0 |
| FirstOf<1>, 16 B, hit at half, against memchr | 0.8 / 1.5 | 1.4 / 3.4 |
| FirstOf<2>, 16 B, hit at half, against string_view::find_first_of | 0.8 / 2.6 | 2.1 / 4.6 |
| FirstOf<2>, 16 B, hit at half, against byte loop | 0.8 / 2.6 | 2.1 / 4.2 |
| find_outside digits, 16 B, last byte, against std::find_if | 0.8 / 4.8 | 1.4 / 5.0 |
| FirstOf<1>, 256 B, no match, against memchr | 4.7 / 5.7 | 6.6 / 5.9 |
| FirstOf<2>, 256 B, no match, against string_view::find_first_of | 5.1 / 86.1 | 9.8 / 101.7 |
| FirstOf<2>, 256 B, no match, against byte loop | 5.1 / 87.4 | 9.7 / 123.5 |
| FirstOf<1>, 256 B, hit at half, against memchr | 2.5 / 3.6 | 4.3 / 5.3 |
| FirstOf<2>, 256 B, hit at half, against string_view::find_first_of | 3.3 / 40.9 | 6.5 / 56.2 |
| FirstOf<2>, 256 B, hit at half, against byte loop | 3.3 / 41.6 | 6.5 / 57.6 |
| find_outside digits, 256 B, last byte, against std::find_if | 4.9 / 75.2 | 8.2 / 90.2 |
| FirstOf<1>, 4096 B, no match, against memchr | 81.4 / 83.8 | 102.8 / 48.6 |
| FirstOf<2>, 4096 B, no match, against string_view::find_first_of | 93.6 / 1,089.4 | 145.0 / 1,497.4 |
| FirstOf<2>, 4096 B, no match, against byte loop | 92.3 / 1,067.1 | 145.6 / 1,936.5 |
| FirstOf<1>, 4096 B, hit at half, against memchr | 43.3 / 44.0 | 57.9 / 26.7 |
| FirstOf<2>, 4096 B, hit at half, against string_view::find_first_of | 53.6 / 546.5 | 78.3 / 737.4 |
| FirstOf<2>, 4096 B, hit at half, against byte loop | 53.4 / 544.3 | 78.0 / 777.2 |
| find_outside digits, 4096 B, last byte, against std::find_if | 80.4 / 1,080.8 | 126.9 / 1,391.9 |

## Where the library is not ahead

- A compiler-vectorized arithmetic loop beats the table lookup of `is_alpha` by 1.3x (M4 Pro)
  and 1.4x (i9-9980HK), and matches `simd::ascii_lower` at 256 B and above.
- `FirstOf<1>` runs at `memchr`'s speed on NEON and up to 2.2x slower than the system `memchr`
  on x86 (4 KB, no match), 1.1x slower at 256 B. One byte is a job for `memchr`.
- `hexdec` (two digits, `-1` on bad input) takes 2.1x (M4 Pro) and 1.5x (i9-9980HK) as long as
  arithmetic. `hex_value` takes 1.2x as long on the M4 Pro and 0.7x as long on the i9, and
  `absl::HexStringToBytes` is faster than both (3.5x and 2.0x faster than `hex_value`).

## The portable backend

`char_classify_compare_swar` is built with `CHARS_NO_SIMD` and shows what the vector backends
buy. Portable time over native time:

| row | M4 Pro | i9-9980HK |
|---|---:|---:|
| escaping kernel, 4 KB printable | 2.0x | 3.1x |
| escaping kernel, 64 KB mixed | 2.4x | 4.1x |
| `FirstOf<2>`, 4 KB | 3.1x | 5.1x |
| `ascii_lower`, 64 KB | 1.8x | 1.6x |

On the portable backend the 64 KB mixed kernel row takes 1.0x (M4 Pro) and 3.1x (i9) the time
of the scalar table loop.
