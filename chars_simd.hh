/*
 * Copyright © 2026 Germán Méndez Bravo (Kronuz)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#pragma once

// Byte-class scanning over whole blocks: find the first byte that is in a set, or outside
// a range (or in a set), and fold ASCII case. One interface, three backends:
//
//   neon   16 bytes per block (AArch64)
//   sse2   16 bytes per block (x86-64)
//   swar   8 bytes per block in a 64-bit integer (everything else, or CHARS_NO_SIMD)
//
// Define CHARS_NO_SIMD before the include to force the portable backend. The backends
// give identical results; `chars::simd::backend` names the one compiled in. The header
// has no dependency beyond the standard library.
//
// A block operation reads exactly `width` bytes at the pointer it is given and never
// anything else; the `find` helpers read only [p, end).

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#if !defined(CHARS_NO_SIMD) && defined(__ARM_NEON) && defined(__aarch64__)
#define CHARS_SIMD_NEON 1
#define CHARS_SIMD_VECTOR 1
#include <arm_neon.h>
#elif !defined(CHARS_NO_SIMD) && (defined(__SSE2__) || defined(_M_X64))
#define CHARS_SIMD_SSE2 1
#define CHARS_SIMD_VECTOR 1
#include <emmintrin.h>
#endif

// Keeps a function out of its callers: a block loop that a small input never reaches stays out of
// the caller's code, registers and inlining budget.
#if defined(_MSC_VER) && !defined(__clang__)
#define CHARS_NOINLINE __declspec(noinline)
#define CHARS_ALWAYS_INLINE __forceinline
#else
#define CHARS_NOINLINE __attribute__((noinline))
#define CHARS_ALWAYS_INLINE inline __attribute__((always_inline))
#endif

namespace chars::simd {

// A Vec holds `width` bytes. A Mask has one bit per byte of a block (spread by `mask_shift`
// bits per byte, so use first/drop_first/count rather than reading bits).
#if defined(CHARS_SIMD_NEON)

inline constexpr const char* backend = "neon";
inline constexpr std::size_t width = 16;
using Vec = uint8x16_t;
using Mask = std::uint64_t;
inline constexpr unsigned mask_shift = 2;   // four bits per byte, one of them set
inline constexpr Mask mask_stop = 0;

inline Vec load(const void* p) noexcept { return vld1q_u8(static_cast<const std::uint8_t*>(p)); }
inline void store(void* p, Vec v) noexcept { vst1q_u8(static_cast<std::uint8_t*>(p), v); }
inline Vec splat(unsigned char c) noexcept { return vdupq_n_u8(c); }

// Per-byte tests give a Vec of lane flags; either() joins two, to_mask() makes the Mask.
inline Vec eq(Vec a, Vec b) noexcept { return vceqq_u8(a, b); }
inline Vec either(Vec a, Vec b) noexcept { return vorrq_u8(a, b); }
inline Mask to_mask(Vec lanes) noexcept {
	return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(lanes), 4)), 0) & 0x1111111111111111ULL;
}

struct Range { Vec lo, span; };
inline Range make_range(unsigned lo, unsigned hi) noexcept {
	return {splat(static_cast<unsigned char>(lo)), splat(static_cast<unsigned char>(hi - lo))};
}
inline Vec outside(Vec x, const Range& r) noexcept { return vcgtq_u8(vsubq_u8(x, r.lo), r.span); }

inline Vec lower(Vec x) noexcept {
	const Vec upper_case = vcleq_u8(vsubq_u8(x, vdupq_n_u8('A')), vdupq_n_u8('Z' - 'A'));
	return vorrq_u8(x, vandq_u8(upper_case, vdupq_n_u8(0x20)));
}
inline Vec upper(Vec x) noexcept {
	const Vec lower_case = vcleq_u8(vsubq_u8(x, vdupq_n_u8('a')), vdupq_n_u8('z' - 'a'));
	return vbicq_u8(x, vandq_u8(lower_case, vdupq_n_u8(0x20)));
}

#elif defined(CHARS_SIMD_SSE2)

inline constexpr const char* backend = "sse2";
inline constexpr std::size_t width = 16;
using Vec = __m128i;
using Mask = std::uint32_t;
inline constexpr unsigned mask_shift = 0;
inline constexpr Mask mask_stop = 0x10000;  // makes first() of an empty mask `width`

inline Vec load(const void* p) noexcept { return _mm_loadu_si128(static_cast<const __m128i*>(p)); }
inline void store(void* p, Vec v) noexcept { _mm_storeu_si128(static_cast<__m128i*>(p), v); }
inline Vec splat(unsigned char c) noexcept { return _mm_set1_epi8(static_cast<char>(c)); }

inline Vec eq(Vec a, Vec b) noexcept { return _mm_cmpeq_epi8(a, b); }
inline Vec either(Vec a, Vec b) noexcept { return _mm_or_si128(a, b); }
inline Mask to_mask(Vec lanes) noexcept { return static_cast<Mask>(_mm_movemask_epi8(lanes)); }

// SSE2 has no unsigned byte compare. Adding 0x80 - lo moves the range to the bottom of the
// signed bytes, [-128, span - 128], so one signed greater-than finds the bytes outside it.
struct Range { Vec add, limit; };
inline Range make_range(unsigned lo, unsigned hi) noexcept {
	return {splat(static_cast<unsigned char>(0x80 - lo)), splat(static_cast<unsigned char>(hi - lo - 0x80))};
}
inline Vec outside(Vec x, const Range& r) noexcept { return _mm_cmpgt_epi8(_mm_add_epi8(x, r.add), r.limit); }

inline Vec lower(Vec x) noexcept {
	const Vec t = _mm_sub_epi8(x, _mm_set1_epi8('A'));
	const Vec upper_case = _mm_cmpeq_epi8(_mm_min_epu8(t, _mm_set1_epi8('Z' - 'A')), t);
	return _mm_or_si128(x, _mm_and_si128(upper_case, _mm_set1_epi8(0x20)));
}
inline Vec upper(Vec x) noexcept {
	const Vec t = _mm_sub_epi8(x, _mm_set1_epi8('a'));
	const Vec lower_case = _mm_cmpeq_epi8(_mm_min_epu8(t, _mm_set1_epi8('z' - 'a')), t);
	return _mm_andnot_si128(_mm_and_si128(lower_case, _mm_set1_epi8(0x20)), x);
}

#else

// Eight bytes in a 64-bit integer, lane 0 in the least significant byte whatever the byte
// order. The tests are exact in every lane: the carry of one lane never reaches the next,
// so a mask never has a false positive above a true one.
inline constexpr const char* backend = "swar";
inline constexpr std::size_t width = 8;
using Vec = std::uint64_t;
using Mask = std::uint64_t;
inline constexpr unsigned mask_shift = 3;   // the high bit of each byte
inline constexpr Mask mask_stop = 0;

inline constexpr std::uint64_t ones = 0x0101010101010101ULL;
inline constexpr std::uint64_t highs = 0x8080808080808080ULL;
inline constexpr std::uint64_t lows = 0x7f7f7f7f7f7f7f7fULL;

inline constexpr std::uint64_t byteswap(std::uint64_t v) noexcept {
	v = ((v & 0x00ff00ff00ff00ffULL) << 8) | ((v >> 8) & 0x00ff00ff00ff00ffULL);
	v = ((v & 0x0000ffff0000ffffULL) << 16) | ((v >> 16) & 0x0000ffff0000ffffULL);
	return (v << 32) | (v >> 32);
}
inline Vec load(const void* p) noexcept {
	std::uint64_t v;
	std::memcpy(&v, p, 8);
	if constexpr (std::endian::native == std::endian::big) { v = byteswap(v); }
	return v;
}
inline void store(void* p, Vec v) noexcept {
	if constexpr (std::endian::native == std::endian::big) { v = byteswap(v); }
	std::memcpy(p, &v, 8);
}
inline Vec splat(unsigned char c) noexcept { return ones * c; }

// The high bit of a lane is set when the lane is zero (bits below it are clear).
inline Vec eq(Vec a, Vec b) noexcept {
	const std::uint64_t z = a ^ b;
	return ~(((z & lows) + lows) | z | lows);
}
inline Vec either(Vec a, Vec b) noexcept { return a | b; }
inline Mask to_mask(Vec lanes) noexcept { return lanes & highs; }

// Needs hi <= 0x7f: the 7-bit part of each lane is compared against the range, and a lane
// with its high bit set is outside it.
struct Range { std::uint64_t ge, gt; };
inline Range make_range(unsigned lo, unsigned hi) noexcept {
	assert(hi <= 0x7f);
	return {ones * (0x80 - lo), ones * (0x7f - hi)};
}
inline Vec outside(Vec x, const Range& r) noexcept {
	const std::uint64_t h = x & lows;
	return (x | ~(h + r.ge) | (h + r.gt)) & highs;
}

inline Vec lower(Vec x) noexcept {
	const std::uint64_t h = x & lows;
	const std::uint64_t upper_case = (h + ones * (0x80 - 'A')) & ~(h + ones * (0x7f - 'Z')) & ~x & highs;
	return x | (upper_case >> 2);
}
inline Vec upper(Vec x) noexcept {
	const std::uint64_t h = x & lows;
	const std::uint64_t lower_case = (h + ones * (0x80 - 'a')) & ~(h + ones * (0x7f - 'z')) & ~x & highs;
	return x & ~(lower_case >> 2);
}

#endif

// Index of the first byte flagged in `m`, or `width` when none is.
inline unsigned first(Mask m) noexcept { return static_cast<unsigned>(std::countr_zero(m | mask_stop)) >> mask_shift; }
// `m` without its first flagged byte.
inline Mask drop_first(Mask m) noexcept { return m & (m - 1); }
// How many bytes `m` flags.
#if defined(CHARS_SIMD_SSE2) && !defined(__POPCNT__)
// Baseline x86-64 has no popcnt instruction, and std::popcount becomes a dozen shifts (or a
// library call): two byte-table reads count the 16 bits cheaper.
namespace detail {
struct PopTable {
	unsigned char v[256];
	constexpr PopTable() noexcept : v() {
		for (int i = 0; i < 256; ++i) { v[i] = static_cast<unsigned char>((i & 1) + v[i >> 1]); }
	}
};
inline constexpr PopTable pop_table{};
}  // namespace detail
inline unsigned count(Mask m) noexcept { return detail::pop_table.v[m & 0xff] + detail::pop_table.v[(m >> 8) & 0xff]; }
#else
inline unsigned count(Mask m) noexcept { return static_cast<unsigned>(std::popcount(m)); }
#endif


// The bytes of a block that equal any of N values. `find` is exact at any length.
//
//   chars::simd::FirstOf<2> amp_eq('&', '=');
//   const char* hit = amp_eq.find(p, end);       // first '&' or '=' in [p, end), or end
template <std::size_t N>
class FirstOf {
	static_assert(N > 0, "FirstOf needs at least one byte");
	std::array<Vec, N> rep_;
	std::array<unsigned char, N> set_;

public:
	template <typename... C>
	requires (sizeof...(C) == N && (std::is_arithmetic_v<C> && ...))
	explicit FirstOf(C... c) noexcept
		: rep_{splat(static_cast<unsigned char>(c))...}, set_{static_cast<unsigned char>(c)...} {}

	// The bytes of the block `x` that are in the set.
	Mask hits(Vec x) const noexcept {
		Vec h = eq(x, rep_[0]);
		for (std::size_t i = 1; i < N; ++i) { h = either(h, eq(x, rep_[i])); }
		return to_mask(h);
	}
	Mask hits(const void* p) const noexcept { return hits(load(p)); }
	// Index of the first byte in the set among the `width` bytes at p, or `width`.
	std::size_t first(const void* p) const noexcept { return simd::first(hits(p)); }
	bool contains(unsigned char c) const noexcept {
		for (std::size_t i = 0; i < N; ++i) { if (c == set_[i]) { return true; } }
		return false;
	}
	// First byte of [p, end) in the set, or end.
	const char* find(const char* p, const char* end) const noexcept {
		if (static_cast<std::size_t>(end - p) < width) {
			for (; p != end; ++p) { if (contains(static_cast<unsigned char>(*p))) { return p; } }
			return end;
		}
		const char* const last = end - width;  // the final block overlaps bytes already known clean
		for (; p < last; p += width) {
			const Mask m = hits(p);
			if (m) { return p + simd::first(m); }
		}
		const Mask m = hits(last);
		return m ? last + simd::first(m) : end;
	}
};
template <typename... C> FirstOf(C...) -> FirstOf<sizeof...(C)>;


// The bytes of a block that are outside [lo, hi] or equal to any of N values. The range
// lies in ASCII (hi <= 0x7f), so a byte with its high bit set is always outside it.
//
//   chars::simd::Outside<2> dirty(0x20, 0x7e, '\\', '\'');   // not printable, backslash or quote
template <std::size_t N>
class Outside {
	Range range_;
	std::array<Vec, N> rep_;
	std::array<unsigned char, N> set_;
	unsigned char lo_, hi_;

public:
	template <typename... C>
	requires (sizeof...(C) == N && (std::is_arithmetic_v<C> && ...))
	Outside(unsigned lo, unsigned hi, C... c) noexcept
		: range_(make_range(lo, hi)), rep_{splat(static_cast<unsigned char>(c))...},
		  set_{static_cast<unsigned char>(c)...}, lo_(static_cast<unsigned char>(lo)), hi_(static_cast<unsigned char>(hi)) {
		assert(lo <= hi && hi <= 0x7f);
	}

	Mask hits(Vec x) const noexcept {
		Vec h = outside(x, range_);
		for (std::size_t i = 0; i < N; ++i) { h = either(h, eq(x, rep_[i])); }
		return to_mask(h);
	}
	Mask hits(const void* p) const noexcept { return hits(load(p)); }
	std::size_t first(const void* p) const noexcept { return simd::first(hits(p)); }
	bool contains(unsigned char c) const noexcept {
		if (c < lo_ || c > hi_) { return true; }
		for (std::size_t i = 0; i < N; ++i) { if (c == set_[i]) { return true; } }
		return false;
	}
	// First byte of [p, end) that is outside the range or in the set, or end.
	const char* find(const char* p, const char* end) const noexcept {
		if (static_cast<std::size_t>(end - p) < width) {
			for (; p != end; ++p) { if (contains(static_cast<unsigned char>(*p))) { return p; } }
			return end;
		}
		const char* const last = end - width;
		for (; p < last; p += width) {
			const Mask m = hits(p);
			if (m) { return p + simd::first(m); }
		}
		const Mask m = hits(last);
		return m ? last + simd::first(m) : end;
	}
};
template <typename... C> Outside(unsigned, unsigned, C...) -> Outside<sizeof...(C)>;

// First byte of [p, end) outside [lo, hi] (hi <= 0x7f), or end.
inline const char* find_outside(const char* p, const char* end, unsigned lo, unsigned hi) noexcept {
	return Outside<0>(lo, hi).find(p, end);
}


// ASCII case folding of n bytes from src to dst. dst may equal src, or not overlap it.
// Bytes outside A-Z (a-z) are copied unchanged.
inline void ascii_lower(const char* src, std::size_t n, char* dst) noexcept {
	if (n < width) {
		for (std::size_t i = 0; i < n; ++i) {
			const unsigned char c = static_cast<unsigned char>(src[i]);
			dst[i] = static_cast<char>(c - 'A' < 26u ? c | 0x20 : c);
		}
		return;
	}
	std::size_t i = 0;
	for (; i + width <= n; i += width) { store(dst + i, lower(load(src + i))); }
	if (i != n) { store(dst + n - width, lower(load(src + n - width))); }  // overlaps folded bytes: folding twice is a no-op
}
inline void ascii_upper(const char* src, std::size_t n, char* dst) noexcept {
	if (n < width) {
		for (std::size_t i = 0; i < n; ++i) {
			const unsigned char c = static_cast<unsigned char>(src[i]);
			dst[i] = static_cast<char>(c - 'a' < 26u ? c & ~0x20 : c);
		}
		return;
	}
	std::size_t i = 0;
	for (; i + width <= n; i += width) { store(dst + i, upper(load(src + i))); }
	if (i != n) { store(dst + n - width, upper(load(src + n - width))); }
}

}  // namespace chars::simd
