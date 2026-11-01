#!/usr/bin/env python3
"""Mutation check for the hex table, the block scanners and the escaping kernel.

For each mutant: copy chars.hh, chars_simd.hh and chars_escape.hh with one deliberate bug into
a scratch dir, build the tests that cover that code against the copy, and require that one of
them FAILS, crashes, hangs or stops compiling on a static_assert. A mutant that survives means
the tests would not catch that bug.

A mutant lives in one backend (neon, sse2, swar) or in code several share. The tests are built
twice: with the backend of the target (native) and with CHARS_NO_SIMD (swar). A mutant is run in
the builds that compile its code. One that needs a backend this host does not build is skipped
and reported, not counted as killed: run the tool on an arm64 host (neon), on an x86-64 host
(sse2), or on an Apple silicon Mac with CXXFLAGS="-arch x86_64" (Rosetta runs the sse2 build).

Four changes are left out because they leave the output unchanged: the dense-block threshold
of the kernel (`dense_escapes`, which only picks between two correct writers), the `|` with
`lows` in the swar `eq` (the mask discards those bits), `>=` against `>` for a block loop
that hands the rest to the 8-byte loop, and the quote slot of the writer in `emit_blocks` when
there is no quote (a NUL is outside the printable range already, so the set is the same).

usage: tools/mutants.py TREE_DIR [CXX] [only-name ...]
       env: CXXFLAGS (extra flags), MUT_DIR (scratch dir), JOBS (parallel mutants, default 4), KEEP=1
"""
import concurrent.futures
import os
import shlex
import shutil
import subprocess
import sys
import tempfile

tree = os.path.abspath(sys.argv[1])
cxx = sys.argv[2] if len(sys.argv) > 2 else "c++"
only = set(sys.argv[3:])
extra = shlex.split(os.environ.get("CXXFLAGS", ""))
jobs = int(os.environ.get("JOBS", "4"))

HEADERS = ["chars.hh", "chars_simd.hh", "chars_escape.hh"]
SRC = {h: open(os.path.join(tree, h), encoding="utf-8").read() for h in HEADERS}
TESTS = ["test/test.cc", "test/simd_test.cc", "test/escape_kernel_test.cc"]


def native_backend():
    """The backend an unmodified build selects, from the compiler's own macros."""
    probe = subprocess.run([cxx, "-std=c++20", "-dM", "-E", "-x", "c++", "-"] + extra, input="", capture_output=True, text=True)
    macros = probe.stdout
    if "__ARM_NEON" in macros and "__aarch64__" in macros:
        return "neon"
    if "__SSE2__" in macros or "_M_X64" in macros:
        return "sse2"
    return "swar"


NATIVE = native_backend()

# name: (file, old, new, where, which)
#   where: "all" (built natively and with CHARS_NO_SIMD), "swar" (only the portable backend), "native" (the
#          block kernel, which exists only with a vector backend), "neon", "sse2", or "nosse2" (neon and swar)
#   which: None when `old` occurs once, else the index of the occurrence to replace
MUTANTS = {
	# ---- chars.hh: the byte-wide hex table
	"hex_value_digit": ("chars.hh", "v['0' + i] = static_cast<unsigned char>(i);", "v['0' + i] = static_cast<unsigned char>(i + 1);", "all", None),
	"hex_value_upper": ("chars.hh", "v['a' + i] = v['A' + i] = static_cast<unsigned char>(10 + i);", "v['a' + i] = static_cast<unsigned char>(10 + i);", "all", None),
	"hex_value_range": ("chars.hh", "for (int i = 0; i < 6; ++i) { v['a' + i]", "for (int i = 0; i < 7; ++i) { v['a' + i]", "all", None),
	"hex_value_sentinel": ("chars.hh", "constexpr unsigned char NO_HEX_VALUE = 0xff;", "constexpr unsigned char NO_HEX_VALUE = 0xfe;", "all", None),

	# ---- chars_escape.hh: the scalar rules and the table
	"table_backslash": ("chars_escape.hh", "else if (c == '\\\\') { t.len[i] = 2; }", "else if (c == '\\\\') { t.len[i] = 1; }", "all", None),
	"table_tab_letter": ("chars_escape.hh", "t.rep[i] = 't'; }", "t.rep[i] = 'T'; }", "all", None),
	"table_cr_letter": ("chars_escape.hh", "t.rep[i] = 'r'; }", "t.rep[i] = 'n'; }", "all", None),
	"table_space_escaped": ("chars_escape.hh", "else if (c < ' ' || c >= 0x7f) { t.len[i] = 4; }", "else if (c <= ' ' || c >= 0x7f) { t.len[i] = 4; }", "all", None),
	"table_del_plain": ("chars_escape.hh", "else if (c < ' ' || c >= 0x7f) { t.len[i] = 4; }", "else if (c < ' ' || c > 0x7f) { t.len[i] = 4; }", "all", None),
	"table_high_plain": ("chars_escape.hh", "else if (c < ' ' || c >= 0x7f) { t.len[i] = 4; }", "else if (c < ' ' || c >= 0x80) { t.len[i] = 4; }", "all", None),
	"table_nul_is_quote": ("chars_escape.hh", "else if (quote != '\\0' && c == static_cast<unsigned char>(quote))", "else if (c == static_cast<unsigned char>(quote))", "all", None),
	"table_hex_case": ("chars_escape.hh", "'a' + (n - 10)", "'A' + (n - 10)", "all", None),
	"table_hex_x": ("chars_escape.hh", "t.out[i][1] = 'x';", "t.out[i][1] = 'X';", "all", None),
	"table_hex_low_nibble": ("chars_escape.hh", "escape_detail::hex_digit(c & 15u)", "escape_detail::hex_digit(c & 7u)", "all", None),
	"table_quote8": ("chars_escape.hh", "t.quote8 = 0x0101010101010101ULL * static_cast<unsigned char>(quote);", "t.quote8 = 0x0101010101010101ULL;", "all", None),
	"table_single_is_double": ("chars_escape.hh", "if (quote == '\\'') { return escape_table_single; }", "if (quote == '\\'') { return escape_table_double; }", "all", None),
	"table_custom_not_built": ("chars_escape.hh", "scratch = make_escape_table(quote);", "scratch = make_escape_table('\\0');", "all", None),
	"byte_pair_letter": ("chars_escape.hh", "*d++ = static_cast<char>(t.rep[c]);", "*d++ = 'n';", "all", None),

	# ---- chars_escape.hh: the 8-byte test and loops (the whole kernel on swar, the tails elsewhere)
	"dirty8_quote": ("chars_escape.hh", "has_zero(x ^ q8) |", "", "all", None),
	"dirty8_backslash": ("chars_escape.hh", "return (has_zero(x ^ (ones * '\\\\')) |", "return (", "all", None),
	"dirty8_control": ("chars_escape.hh", "((x - ones * 0x20) & ~x & highs) |", "", "all", None),
	"dirty8_high": ("chars_escape.hh", "(((x + ones) | x) & highs)", "(x & highs)", "all", None),
	"measure_eight_loop": ("chars_escape.hh", "while (e - b >= 8) {\n\t\t\tstd::uint64_t x;\n\t\t\tstd::memcpy(&x, b, 8);\n\t\t\tif (!dirty8(x, q8)) {\n\t\t\t\tn += 8;", "while (e - b >= 7) {\n\t\t\tstd::uint64_t x;\n\t\t\tstd::memcpy(&x, b, 8);\n\t\t\tif (!dirty8(x, q8)) {\n\t\t\t\tn += 8;", "all", None),
	"emit_eight_loop": ("chars_escape.hh", "while (e - b >= 8) {\n\t\t\tstd::uint64_t x;\n\t\t\tstd::memcpy(&x, b, 8);\n\t\t\tif (!dirty8(x, q8)) {\n\t\t\t\tstd::memcpy(d, b, 8);", "while (e - b >= 7) {\n\t\t\tstd::uint64_t x;\n\t\t\tstd::memcpy(&x, b, 8);\n\t\t\tif (!dirty8(x, q8)) {\n\t\t\t\tstd::memcpy(d, b, 8);", "all", None),
	"measure_clean_block": ("chars_escape.hh", "if (!dirty8(x, q8)) {\n\t\t\t\tn += 8;", "if (!dirty8(x, q8)) {\n\t\t\t\tn += 7;", "all", None),
	"emit_clean_block": ("chars_escape.hh", "std::memcpy(d, b, 8);\n\t\t\t\td += 8;", "std::memcpy(d, b, 8);\n\t\t\t\td += 7;", "all", None),
	"emit_dirty_block": ("chars_escape.hh", "for (int i = 0; i < 8; ++i) { d = escape_byte(d, b[i], t); }", "for (int i = 0; i < 7; ++i) { d = escape_byte(d, b[i], t); }", "all", None),
	"measure_dirty_block": ("chars_escape.hh", "for (int i = 0; i < 8; ++i) { n += t.len[b[i]]; }", "for (int i = 0; i < 7; ++i) { n += t.len[b[i]]; }", "all", None),

	# ---- chars_escape.hh: the block kernel (neon and sse2)
	"blocks_no_quote_slot_measure": ("chars_escape.hh", "const char q = t.quote != '\\0' ? t.quote : '\\\\';", "const char q = t.quote;", "native", 0),
	"blocks_emit_no_quote": ("chars_escape.hh", "const simd::Outside<2> dirty(0x20, 0x7e, '\\\\', q);\n\tdo {", "const simd::Outside<2> dirty(0x20, 0x7e, '\\\\', '\\\\');\n\tdo {", "native", None),
	"blocks_range_low": ("chars_escape.hh", "const simd::Outside<2> dirty(0x20, 0x7e, '\\\\', q);\n\tconst simd::FirstOf<5>", "const simd::Outside<2> dirty(0x21, 0x7e, '\\\\', q);\n\tconst simd::FirstOf<5>", "native", None),
	"blocks_range_high": ("chars_escape.hh", "const simd::Outside<2> dirty(0x20, 0x7e, '\\\\', q);\n\tconst simd::FirstOf<5>", "const simd::Outside<2> dirty(0x20, 0x7f, '\\\\', q);\n\tconst simd::FirstOf<5>", "native", None),
	"blocks_emit_range_high": ("chars_escape.hh", "const simd::Outside<2> dirty(0x20, 0x7e, '\\\\', q);\n\tdo {", "const simd::Outside<2> dirty(0x20, 0x7f, '\\\\', q);\n\tdo {", "native", None),
	"blocks_pair_quote": ("chars_escape.hh", "const simd::FirstOf<5> pair('\\n', '\\r', '\\t', '\\\\', q);", "const simd::FirstOf<5> pair('\\n', '\\r', '\\t', '\\\\', '\\0');", "native", None),
	"blocks_pair_tab": ("chars_escape.hh", "const simd::FirstOf<5> pair('\\n', '\\r', '\\t', '\\\\', q);", "const simd::FirstOf<5> pair('\\n', '\\r', '\\\\', '\\\\', q);", "native", None),
	"blocks_hex_weight": ("chars_escape.hh", "simd::width + 3 * simd::count(m) - 2 * simd::count(pair.hits(x))", "simd::width + 2 * simd::count(m) - 2 * simd::count(pair.hits(x))", "native", None),
	"blocks_pair_weight": ("chars_escape.hh", "simd::width + 3 * simd::count(m) - 2 * simd::count(pair.hits(x))", "simd::width + 3 * simd::count(m) - simd::count(pair.hits(x))", "native", None),
	"blocks_clean_weight": ("chars_escape.hh", "n += m == 0 ? simd::width : simd::width", "n += m == 0 ? simd::width - 1 : simd::width", "native", None),
	"blocks_measure_overrun": ("chars_escape.hh", "} while (e - b >= static_cast<std::ptrdiff_t>(simd::width));\n\treturn {n, b};", "} while (e - b >= 1);\n\treturn {n, b};", "native", None),
	"blocks_measure_advance": ("chars_escape.hh", "b += simd::width;\n\t} while (e - b >= static_cast<std::ptrdiff_t>(simd::width));", "b += simd::width - 1;\n\t} while (e - b >= static_cast<std::ptrdiff_t>(simd::width));", "native", None),
	"blocks_clean_advance": ("chars_escape.hh", "simd::store(d, x);\n\t\t\tb += simd::width;\n\t\t\td += simd::width;", "simd::store(d, x);\n\t\t\tb += simd::width;\n\t\t\td += simd::width - 1;", "native", None),
	"blocks_dense_advance": ("chars_escape.hh", "b += simd::width;\n\t\t\tcontinue;\n\t\t}\n\t\tunsigned pos = 0;", "b += simd::width - 1;\n\t\t\tcontinue;\n\t\t}\n\t\tunsigned pos = 0;", "native", None),
	"blocks_dense_len": ("chars_escape.hh", "std::memcpy(d, t.out[c], 4);\n\t\t\t\td += t.len[c];\n\t\t\t}\n\t\t\tb += simd::width;", "std::memcpy(d, t.out[c], 4);\n\t\t\t\td += t.len[c] & 3;\n\t\t\t}\n\t\t\tb += simd::width;", "native", None),
	"blocks_sparse_copy": ("chars_escape.hh", "d += i - pos;", "d += i - pos + 1;", "native", None),
	"blocks_sparse_resume": ("chars_escape.hh", "pos = i + 1;", "pos = i;", "native", None),
	"blocks_sparse_escape_len": ("chars_escape.hh", "std::memcpy(d, t.out[c], 4);\n\t\t\td += t.len[c];\n\t\t\tpos = i + 1;", "std::memcpy(d, t.out[c], 4);\n\t\t\td += 2;\n\t\t\tpos = i + 1;", "native", None),
	"blocks_sparse_rest": ("chars_escape.hh", "d += simd::width - pos;", "d += simd::width - pos - 1;", "native", None),
	"blocks_sparse_no_rest": ("chars_escape.hh", "simd::store(d, simd::load(b + pos));  // the rest", "// the rest", "native", None),
	"blocks_emit_loop": ("chars_escape.hh", "} while (e - b >= static_cast<std::ptrdiff_t>(2 * simd::width));\n\treturn {d, b};", "} while (e - b >= static_cast<std::ptrdiff_t>(simd::width));\n\treturn {d, b};", "native", None),
	"blocks_emit_entry": ("chars_escape.hh", "if (e - b >= static_cast<std::ptrdiff_t>(2 * simd::width)) {\n\t\t\tconst Emitted r", "if (e - b >= static_cast<std::ptrdiff_t>(simd::width)) {\n\t\t\tconst Emitted r", "native", None),
	"blocks_emit_drops_input": ("chars_escape.hh", "d = r.d;\n\t\t\tb = r.b;", "d = r.d;", "native", None),
	"blocks_measure_drops_input": ("chars_escape.hh", "n = r.n;\n\t\t\tb = r.b;", "n = r.n;", "native", None),

	# ---- chars_simd.hh: mask operations and the scanners every backend shares
	"first_shift": ("chars_simd.hh", "std::countr_zero(m | mask_stop)) >> mask_shift;", "std::countr_zero(m | mask_stop));", "nosse2", None),
	"drop_first": ("chars_simd.hh", "inline Mask drop_first(Mask m) noexcept { return m & (m - 1); }", "inline Mask drop_first(Mask m) noexcept { return m & (m + 1); }", "all", None),
	"count_popcount": ("chars_simd.hh", "inline unsigned count(Mask m) noexcept { return static_cast<unsigned>(std::popcount(m)); }", "inline unsigned count(Mask m) noexcept { return static_cast<unsigned>(std::popcount(m)) - (m != 0 ? 1u : 0u); }", "nosse2", None),
	"first_of_tail_block": ("chars_simd.hh", "const char* const last = end - width;  // the final block", "const char* const last = end - width + 1;  // the final block", "all", None),
	"outside_tail_block": ("chars_simd.hh", "const char* const last = end - width;\n\t\tfor", "const char* const last = end - width + 1;\n\t\tfor", "all", None),
	"first_of_stride": ("chars_simd.hh", "for (; p < last; p += width) {", "for (; p < last - 1; p += width) {", "all", 0),
	"outside_stride": ("chars_simd.hh", "for (; p < last; p += width) {", "for (; p < last - 1; p += width) {", "all", 1),
	"first_of_contains": ("chars_simd.hh", "for (std::size_t i = 0; i < N; ++i) { if (c == set_[i]) { return true; } }\n\t\treturn false;\n\t}\n\t// First byte of [p, end) in the set", "for (std::size_t i = 1; i < N; ++i) { if (c == set_[i]) { return true; } }\n\t\treturn false;\n\t}\n\t// First byte of [p, end) in the set", "all", None),
	"first_of_hits_last": ("chars_simd.hh", "for (std::size_t i = 1; i < N; ++i) { h = either(h, eq(x, rep_[i])); }", "for (std::size_t i = 1; i + 1 < N; ++i) { h = either(h, eq(x, rep_[i])); }", "all", None),
	"outside_hits_first": ("chars_simd.hh", "for (std::size_t i = 0; i < N; ++i) { h = either(h, eq(x, rep_[i])); }", "for (std::size_t i = 1; i < N; ++i) { h = either(h, eq(x, rep_[i])); }", "all", None),
	"outside_contains_low": ("chars_simd.hh", "if (c < lo_ || c > hi_) { return true; }", "if (c <= lo_ || c > hi_) { return true; }", "all", None),
	"outside_contains_high": ("chars_simd.hh", "if (c < lo_ || c > hi_) { return true; }", "if (c < lo_ || c >= hi_) { return true; }", "all", None),
	"lower_tail_block": ("chars_simd.hh", "store(dst + n - width, lower(load(src + n - width)));", "store(dst + n - width + 1, lower(load(src + n - width + 1)));", "all", None),
	"upper_tail_block": ("chars_simd.hh", "store(dst + n - width, upper(load(src + n - width)));", "store(dst + n - width + 1, upper(load(src + n - width + 1)));", "all", None),
	"lower_short": ("chars_simd.hh", "c - 'A' < 26u ? c | 0x20 : c", "c - 'A' < 27u ? c | 0x20 : c", "all", None),
	"upper_short": ("chars_simd.hh", "c - 'a' < 26u ? c & ~0x20 : c", "c - 'a' < 27u ? c & ~0x20 : c", "all", None),

	# ---- chars_simd.hh: neon
	"neon_mask_bits": ("chars_simd.hh", "& 0x1111111111111111ULL;", "& 0x3333333333333333ULL;", "neon", None),
	"neon_outside_inclusive": ("chars_simd.hh", "vcgtq_u8(vsubq_u8(x, r.lo), r.span)", "vcgeq_u8(vsubq_u8(x, r.lo), r.span)", "neon", None),
	"neon_range_span": ("chars_simd.hh", "splat(static_cast<unsigned char>(hi - lo))};", "splat(static_cast<unsigned char>(hi - lo + 1))};", "neon", None),
	"neon_lower_range": ("chars_simd.hh", "vcleq_u8(vsubq_u8(x, vdupq_n_u8('A')), vdupq_n_u8('Z' - 'A'))", "vcleq_u8(vsubq_u8(x, vdupq_n_u8('A')), vdupq_n_u8('Z' - 'A' + 1))", "neon", None),
	"neon_upper_bic": ("chars_simd.hh", "return vbicq_u8(x, vandq_u8(lower_case, vdupq_n_u8(0x20)));", "return vorrq_u8(x, vandq_u8(lower_case, vdupq_n_u8(0x20)));", "neon", None),
	"neon_eq": ("chars_simd.hh", "inline Vec eq(Vec a, Vec b) noexcept { return vceqq_u8(a, b); }", "inline Vec eq(Vec a, Vec b) noexcept { return vceqq_u8(a, vdupq_n_u8(0)); }", "neon", None),

	# ---- chars_simd.hh: sse2
	"sse2_range_add": ("chars_simd.hh", "splat(static_cast<unsigned char>(0x80 - lo))", "splat(static_cast<unsigned char>(0x7f - lo))", "sse2", None),
	"sse2_range_limit": ("chars_simd.hh", "splat(static_cast<unsigned char>(hi - lo - 0x80))", "splat(static_cast<unsigned char>(hi - lo - 0x7f))", "sse2", None),
	"sse2_outside_compare": ("chars_simd.hh", "_mm_cmpgt_epi8(_mm_add_epi8(x, r.add), r.limit)", "_mm_cmpeq_epi8(_mm_add_epi8(x, r.add), r.limit)", "sse2", None),
	"sse2_mask_stop": ("chars_simd.hh", "inline constexpr Mask mask_stop = 0x10000;", "inline constexpr Mask mask_stop = 0;", "sse2", None),
	"sse2_to_mask": ("chars_simd.hh", "static_cast<Mask>(_mm_movemask_epi8(lanes))", "static_cast<Mask>(_mm_movemask_epi8(lanes)) & 0x7fff", "sse2", None),
	"sse2_lower_range": ("chars_simd.hh", "_mm_min_epu8(t, _mm_set1_epi8('Z' - 'A'))", "_mm_min_epu8(t, _mm_set1_epi8('Z' - 'A' + 1))", "sse2", None),
	"sse2_upper_andnot": ("chars_simd.hh", "return _mm_andnot_si128(_mm_and_si128(lower_case, _mm_set1_epi8(0x20)), x);", "return _mm_or_si128(_mm_and_si128(lower_case, _mm_set1_epi8(0x20)), x);", "sse2", None),
	"sse2_pop_second_byte": ("chars_simd.hh", "detail::pop_table.v[(m >> 8) & 0xff]", "detail::pop_table.v[(m >> 9) & 0xff]", "sse2", None),
	"sse2_pop_table": ("chars_simd.hh", "v[i] = static_cast<unsigned char>((i & 1) + v[i >> 1]);", "v[i] = static_cast<unsigned char>((i & 1) + v[i >> 2]);", "sse2", None),

	# ---- chars_simd.hh: swar (the portable backend)
	"swar_eq": ("chars_simd.hh", "const std::uint64_t z = a ^ b;", "const std::uint64_t z = a ^ ~b;", "swar", None),
	"swar_eq_inexact": ("chars_simd.hh", "return ~(((z & lows) + lows) | z | lows);", "return (z - ones) & ~z & highs;", "swar", None),
	"swar_outside_high": ("chars_simd.hh", "return (x | ~(h + r.ge) | (h + r.gt)) & highs;", "return (~(h + r.ge) | (h + r.gt)) & highs;", "swar", None),
	"swar_range_hi": ("chars_simd.hh", "return {ones * (0x80 - lo), ones * (0x7f - hi)};", "return {ones * (0x80 - lo), ones * (0x7e - hi)};", "swar", None),
	"swar_range_lo": ("chars_simd.hh", "return {ones * (0x80 - lo), ones * (0x7f - hi)};", "return {ones * (0x81 - lo), ones * (0x7f - hi)};", "swar", None),
	"swar_lower_shift": ("chars_simd.hh", "return x | (upper_case >> 2);", "return x | (upper_case >> 3);", "swar", None),
	"swar_lower_range": ("chars_simd.hh", "(h + ones * (0x7f - 'Z'))", "(h + ones * (0x7e - 'Z'))", "swar", None),
	"swar_upper_shift": ("chars_simd.hh", "return x & ~(lower_case >> 2);", "return x & ~(lower_case >> 3);", "swar", None),
	"swar_upper_range": ("chars_simd.hh", "(h + ones * (0x80 - 'a'))", "(h + ones * (0x81 - 'a'))", "swar", None),
	"swar_to_mask": ("chars_simd.hh", "inline Mask to_mask(Vec lanes) noexcept { return lanes & highs; }", "inline Mask to_mask(Vec lanes) noexcept { return lanes & lows; }", "swar", None),
}

if len(MUTANTS) < 80:
    raise SystemExit("mutant table shrank")

NATIVE_BUILD = ("native", [])
SWAR_BUILD = ("swar", ["-DCHARS_NO_SIMD=1"])


def builds_for(where):
    """The builds that compile a mutant's code, or None when this host has none of them."""
    if where == "all":
        return [NATIVE_BUILD, SWAR_BUILD]
    if where == "swar":
        return [SWAR_BUILD]
    if where == "native":
        return [NATIVE_BUILD] if NATIVE != "swar" else None
    if where == "nosse2":
        return [NATIVE_BUILD, SWAR_BUILD] if NATIVE == "neon" else [SWAR_BUILD]
    if where == NATIVE:
        return [NATIVE_BUILD]
    return None


STATIC_FAILS = ("static assertion failed", "non-constant condition for static assertion", "static assertion expression is not",
                "static_assert failed", "is not a constant expression")


def build_and_run(workdir, build, tests):
    """The verdict for the first failing test, or None when every test passes."""
    build_name, flags = build
    for test in tests:
        exe = os.path.join(workdir, os.path.basename(test).replace(".cc", "") + "_" + build_name)
        cmd = [cxx, "-std=c++20", "-O1", "-I" + workdir] + extra + flags + [os.path.join(tree, test), "-o", exe]
        built = subprocess.run(cmd, capture_output=True, text=True)
        if built.returncode != 0:
            if any(m in built.stderr for m in STATIC_FAILS):
                return f"killed by {test} ({build_name}): a static_assert fails"
            return f"BROKEN: {test} ({build_name}) does not compile\n{built.stderr[:600]}"
        try:
            ran = subprocess.run([exe], capture_output=True, text=True, timeout=240)
        except subprocess.TimeoutExpired:
            return f"killed by {test} ({build_name}): does not finish"
        if ran.returncode != 0:
            return f"killed by {test} ({build_name}): exit {ran.returncode}"
    return None


work = os.environ.get("MUT_DIR") or tempfile.mkdtemp(prefix="chars_mutants_")
os.makedirs(work, exist_ok=True)

# The unmodified headers must pass in every build, or "killed" would mean nothing.
base = os.path.join(work, "base")
os.makedirs(base, exist_ok=True)
for h in HEADERS:
    shutil.copy(os.path.join(tree, h), os.path.join(base, h))
for build in (NATIVE_BUILD, SWAR_BUILD):
    verdict = build_and_run(base, build, TESTS)
    if verdict:
        print(f"the unmodified headers fail the tests ({build[0]}): {verdict}")
        sys.exit(2)
print(f"unmodified headers pass; native backend: {NATIVE}")


def run(name):
    fname, old, new, where, which = MUTANTS[name]
    builds = builds_for(where)
    if builds is None:
        return name, "skipped", f"needs the {where} backend; native is {NATIVE}"
    text = SRC[fname]
    n = text.count(old)
    if (which is None and n != 1) or (which is not None and n <= which):
        return name, "broken", f"pattern occurs {n} times, fix the table"
    start = -1
    for _ in range((which or 0) + 1):
        start = text.index(old, start + 1)
    mutated = text[:start] + new + text[start + len(old):]
    d = os.path.join(work, name)
    os.makedirs(d, exist_ok=True)
    for h in HEADERS:
        with open(os.path.join(d, h), "w", encoding="utf-8") as f:
            f.write(mutated if h == fname else SRC[h])
    verdict = None
    for build in builds:
        verdict = build_and_run(d, build, TESTS)
        if verdict:
            break
    if not os.environ.get("KEEP"):
        shutil.rmtree(d, ignore_errors=True)
    if verdict is None:
        return name, "survived", "SURVIVED"
    if verdict.startswith("BROKEN"):
        return name, "broken", verdict
    return name, "killed", verdict


names = [n for n in MUTANTS if not only or n in only]
tally = {"killed": [], "survived": [], "skipped": [], "broken": []}
with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
    for name, status, message in pool.map(run, names):
        print(f"{name}: {message}", flush=True)
        tally[status].append(name)

print(f"{len(tally['killed'])} killed, {len(tally['survived'])} survived, {len(tally['skipped'])} skipped, {len(tally['broken'])} broken")
if tally["survived"]:
    print("survivors:", ", ".join(tally["survived"]))
if tally["broken"]:
    print("broken:", ", ".join(tally["broken"]))
if tally["survived"] or tally["broken"]:
    sys.exit(1)
print("all mutants killed" if not tally["skipped"] else "all mutants killed that this host builds")
if not os.environ.get("MUT_DIR"):
    shutil.rmtree(work, ignore_errors=True)
