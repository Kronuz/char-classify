/*
 * Copyright © 2026 Germán Méndez Bravo (Kronuz)
 * Copyright (c) 2015-2020 Dubalu LLC
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

// The C-style escaping kernel shared by escape() and repr(): a length table, a measuring
// pass and a writing pass over a byte range.
//
// A byte is written as itself, as a backslash escape or as \xNN:
//
//   \n \r \t            \n \r \t
//   backslash, quote    a backslash before the byte (the quote only when there is one)
//   < 0x20, >= 0x7f     \xNN, two lowercase hex digits
//   anything else       itself
//
// Callers size the output once with escape_measure() and write it with escape_emit(), which
// writes exactly the measured bytes, so there is no bounds check per byte. Blocks of plain
// printable bytes are tested and copied whole; the escapes of any other block are found from
// its mask and written from the table. The vector backends (see chars_simd.hh) test 16 bytes
// at a time; the portable one tests 8.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "chars.hh"
#include "chars_simd.hh"

namespace chars {

struct EscapeTable {
	unsigned char len[256];     // bytes written for a byte: 1, 2 or 4
	unsigned char rep[256];     // second byte of a two-byte escape
	unsigned char out[256][4];  // the bytes written for a byte (the rest of the four is zero)
	char quote;                 // the quote byte that gets a backslash, '\0' for none
	std::uint64_t quote8;       // the quote in each of the eight bytes of a word
};

namespace escape_detail {

constexpr char hex_digit(unsigned n) noexcept { return static_cast<char>(n < 10 ? '0' + n : 'a' + (n - 10)); }

}  // namespace escape_detail

constexpr EscapeTable make_escape_table(char quote) noexcept {
	EscapeTable t{};
	t.quote = quote;
	t.quote8 = 0x0101010101010101ULL * static_cast<unsigned char>(quote);
	for (int i = 0; i < 256; ++i) {
		const unsigned char c = static_cast<unsigned char>(i);
		t.len[i] = 1;
		t.rep[i] = c;
		if (c == '\n') { t.len[i] = 2; t.rep[i] = 'n'; }
		else if (c == '\r') { t.len[i] = 2; t.rep[i] = 'r'; }
		else if (c == '\t') { t.len[i] = 2; t.rep[i] = 't'; }
		else if (c == '\\') { t.len[i] = 2; }
		else if (quote != '\0' && c == static_cast<unsigned char>(quote)) { t.len[i] = 2; }
		else if (c < ' ' || c >= 0x7f) { t.len[i] = 4; }
		switch (t.len[i]) {
			case 1:
				t.out[i][0] = c;
				break;
			case 2:
				t.out[i][0] = '\\';
				t.out[i][1] = t.rep[i];
				break;
			default:
				t.out[i][0] = '\\';
				t.out[i][1] = 'x';
				t.out[i][2] = static_cast<unsigned char>(escape_detail::hex_digit(c >> 4));
				t.out[i][3] = static_cast<unsigned char>(escape_detail::hex_digit(c & 15u));
				break;
		}
	}
	return t;
}

inline constexpr EscapeTable escape_table_none = make_escape_table('\0');
inline constexpr EscapeTable escape_table_single = make_escape_table('\'');
inline constexpr EscapeTable escape_table_double = make_escape_table('"');

// The table for `quote` ('\0', '\'' or '"' are shared; any other byte is built into `scratch`).
CHARS_NOINLINE inline const EscapeTable& escape_table_custom(char quote, EscapeTable& scratch) noexcept {
	scratch = make_escape_table(quote);
	return scratch;
}

inline const EscapeTable& escape_table(char quote, EscapeTable& scratch) noexcept {
	if (quote == '\0') { return escape_table_none; }
	if (quote == '\'') { return escape_table_single; }
	if (quote == '"') { return escape_table_double; }
	return escape_table_custom(quote, scratch);
}

namespace escape_detail {

inline constexpr std::uint64_t ones = 0x0101010101010101ULL;
inline constexpr std::uint64_t highs = 0x8080808080808080ULL;

constexpr std::uint64_t has_zero(std::uint64_t v) noexcept { return (v - ones) & ~v & highs; }

// True when any of the 8 bytes needs an escape: < 0x20, >= 0x7f, a backslash or the quote.
// Exact as a boolean (carries only come from bytes that are flagged anyway).
inline bool dirty8(std::uint64_t x, std::uint64_t q8) noexcept {
	return (has_zero(x ^ (ones * '\\')) | has_zero(x ^ q8) |
		((x - ones * 0x20) & ~x & highs) | (((x + ones) | x) & highs)) != 0;
}

}  // namespace escape_detail

#if defined(CHARS_SIMD_VECTOR)
namespace escape_detail {

// A block with at least this many escapes is written byte by byte through the table.
inline constexpr unsigned dense_escapes = 6;

struct Measured { std::size_t n; const unsigned char* b; };
struct Emitted { char* d; const unsigned char* b; };

// Measures the whole blocks of [b, e) (at least one) and returns their size and where they end.
// A block is 16 bytes, one output byte each, plus two for a two-byte escape and three for a
// four-byte one. Every two-byte escape is flagged by `dirty` too, so the bytes to add are
// 3 * dirty - 2 * pair; a clean block, the common one, skips the second test.
CHARS_NOINLINE inline Measured measure_blocks(const unsigned char* b, const unsigned char* e, const EscapeTable& t) noexcept {
	// With no quote the quote slot repeats the backslash (a NUL is escaped as \x00, not as \0).
	const char q = t.quote != '\0' ? t.quote : '\\';
	const simd::Outside<2> dirty(0x20, 0x7e, '\\', q);
	const simd::FirstOf<5> pair('\n', '\r', '\t', '\\', q);
	std::size_t n = 0;
	do {
		const simd::Vec x = simd::load(b);
		const simd::Mask m = dirty.hits(x);
		n += m == 0 ? simd::width : simd::width + 3 * simd::count(m) - 2 * simd::count(pair.hits(x));
		b += simd::width;
	} while (e - b >= static_cast<std::ptrdiff_t>(simd::width));
	return {n, b};
}

// Writes the escape of the whole blocks of [b, e) at d, for as long as two blocks remain (the
// caller checks that two do on entry), and returns where the output and the input end.
//
// The escapes of a block are found once, from one mask. With few of them, the bytes before each
// escape are copied with a whole-block store (a load at the first byte not yet written) and the
// output advances only past the ones that are real; the escape then goes out as the four table
// bytes, advancing by its length. With many, every byte of the block goes through its table
// entry (a plain byte's entry is the byte itself). Each store overshoots the bytes it means to
// write, and the next store overwrites the overshoot.
//
// While at least two blocks remain, every store is inside the room at d: a block store at byte p
// needs `width` bytes, the input still holds at least that many from p, and each input byte takes
// at least one output byte, so the room is as large; the table store after an escape at byte i
// needs 4 and the room holds 2 for the escape and one for each input byte after it, at least 16.
CHARS_NOINLINE inline Emitted emit_blocks(char* d, const unsigned char* b, const unsigned char* e, const EscapeTable& t) noexcept {
	const char q = t.quote != '\0' ? t.quote : '\\';
	const simd::Outside<2> dirty(0x20, 0x7e, '\\', q);
	do {
		const simd::Vec x = simd::load(b);
		simd::Mask m = dirty.hits(x);
		if (m == 0) {
			simd::store(d, x);
			b += simd::width;
			d += simd::width;
			continue;
		}
		if (simd::count(m) >= dense_escapes) {
			for (unsigned k = 0; k < simd::width; ++k) {
				const unsigned char c = b[k];
				std::memcpy(d, t.out[c], 4);
				d += t.len[c];
			}
			b += simd::width;
			continue;
		}
		unsigned pos = 0;  // the first byte of the block not yet written
		do {
			const unsigned i = simd::first(m);
			m = simd::drop_first(m);
			simd::store(d, simd::load(b + pos));
			d += i - pos;
			const unsigned char c = b[i];
			std::memcpy(d, t.out[c], 4);
			d += t.len[c];
			pos = i + 1;
		} while (m != 0);
		simd::store(d, simd::load(b + pos));  // the rest of the block, which can be empty (pos == width)
		d += simd::width - pos;
		b += simd::width;
	} while (e - b >= static_cast<std::ptrdiff_t>(2 * simd::width));
	return {d, b};
}

}  // namespace escape_detail
#endif

// Writes the escape of one byte (exactly t.len[c] bytes) at d and returns the end.
inline char* escape_byte(char* d, unsigned char c, const EscapeTable& t) noexcept {
	switch (t.len[c]) {
		case 1:
			*d++ = static_cast<char>(c);
			break;
		case 2:
			*d++ = '\\';
			*d++ = static_cast<char>(t.rep[c]);
			break;
		default:
			*d++ = '\\';
			*d++ = 'x';
			chars::char_repr(static_cast<char>(c), &d);
			break;
	}
	return d;
}

// The bytes [b, e) take once escaped (the quotes around the whole are not counted).
CHARS_ALWAYS_INLINE std::size_t escape_measure(const unsigned char* b, const unsigned char* e, const EscapeTable& t) noexcept {
	using namespace escape_detail;
	std::size_t n = 0;
	if (e - b >= 8) {
#if defined(CHARS_SIMD_VECTOR)
		if (e - b >= static_cast<std::ptrdiff_t>(simd::width)) {
			const Measured r = measure_blocks(b, e, t);
			n = r.n;
			b = r.b;
		}
#endif
		const std::uint64_t q8 = t.quote8;
		while (e - b >= 8) {
			std::uint64_t x;
			std::memcpy(&x, b, 8);
			if (!dirty8(x, q8)) {
				n += 8;
			} else {
				for (int i = 0; i < 8; ++i) { n += t.len[b[i]]; }
			}
			b += 8;
		}
	}
	while (b != e) { n += t.len[*b++]; }
	return n;
}

// Writes the escape of [b, e) at d and returns the end. The room at d must be at least
// escape_measure(b, e, t) bytes (each input byte takes at least one, which is all the
// block-wise stores below rely on).
CHARS_ALWAYS_INLINE char* escape_emit(char* d, const unsigned char* b, const unsigned char* e, const EscapeTable& t) noexcept {
	using namespace escape_detail;
	if (e - b >= 8) {
#if defined(CHARS_SIMD_VECTOR)
		if (e - b >= static_cast<std::ptrdiff_t>(2 * simd::width)) {
			const Emitted r = emit_blocks(d, b, e, t);
			d = r.d;
			b = r.b;
		}
#endif
		const std::uint64_t q8 = t.quote8;
		while (e - b >= 8) {
			std::uint64_t x;
			std::memcpy(&x, b, 8);
			if (!dirty8(x, q8)) {
				std::memcpy(d, b, 8);
				d += 8;
			} else {
				for (int i = 0; i < 8; ++i) { d = escape_byte(d, b[i], t); }
			}
			b += 8;
		}
	}
	while (b != e) { d = escape_byte(d, *b++, t); }
	return d;
}

}  // namespace chars
