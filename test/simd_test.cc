// Tests for chars_simd.hh: find-first-of, outside-a-range-or-in-a-set, and ASCII case fold.
//
// Every result is compared with a scalar reference written here. The same file is built twice
// (CMakeLists.txt): with the backend of the target (NEON, SSE2) and with CHARS_NO_SIMD, which
// forces the portable 64-bit backend, so both must agree with the reference.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <tuple>
#include <vector>

#include "chars_simd.hh"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

namespace simd = chars::simd;
using Bytes = std::vector<unsigned char>;

static std::mt19937_64 rng(12345);

static void test_backend() {
#if defined(CHARS_NO_SIMD)
	CHECK(std::strcmp(simd::backend, "swar") == 0);
	CHECK(simd::width == 8);
#elif defined(__aarch64__) && defined(__ARM_NEON)
	CHECK(std::strcmp(simd::backend, "neon") == 0);
	CHECK(simd::width == 16);
#elif defined(__SSE2__)
	CHECK(std::strcmp(simd::backend, "sse2") == 0);
	CHECK(simd::width == 16);
#else
	CHECK(std::strcmp(simd::backend, "swar") == 0);
#endif
	std::printf("backend %s, %zu bytes per block\n", simd::backend, simd::width);
}

// The lanes of a Mask, in order.
static std::vector<unsigned> lanes(simd::Mask m) {
	std::vector<unsigned> v;
	const unsigned n = simd::count(m);
	while (m) {
		v.push_back(simd::first(m));
		m = simd::drop_first(m);
	}
	CHECK(v.size() == n);
	return v;
}

static void test_mask_ops() {
	CHECK(simd::first(0) == simd::width);
	CHECK(simd::count(0) == 0);
	// A byte at lane i flagged: the mask of FirstOf<1> has exactly that lane.
	const simd::FirstOf<1> f(static_cast<unsigned char>(0x5a));
	for (unsigned i = 0; i < simd::width; ++i) {
		Bytes blk(simd::width, 'a');
		blk[i] = 0x5a;
		const simd::Mask m = f.hits(blk.data());
		CHECK(simd::first(m) == i);
		CHECK(simd::count(m) == 1);
		CHECK(simd::drop_first(m) == 0);
	}
	Bytes blk(simd::width, 0x5a);
	const simd::Mask all = f.hits(blk.data());
	CHECK(simd::count(all) == simd::width);
	CHECK(lanes(all).size() == simd::width);
	for (unsigned i = 0; i < simd::width; ++i) { CHECK(lanes(all)[i] == i); }
}

// Interesting bytes for a set: the members, their neighbours, and the ends of the byte range
// (a SWAR zero-byte test goes wrong next to a matching byte, at 0x00/0x01 and at the high bit).
static unsigned char pick(const std::vector<unsigned char>& set) {
	switch (rng() % 4) {
		case 0: return set.empty() ? static_cast<unsigned char>(rng()) : set[rng() % set.size()];
		case 1: return set.empty() ? static_cast<unsigned char>(rng()) : static_cast<unsigned char>(set[rng() % set.size()] + (rng() % 2 ? 1 : -1));
		case 2: { static const unsigned char e[] = {0x00, 0x01, 0x1f, 0x20, 0x7e, 0x7f, 0x80, 0x81, 0xfe, 0xff}; return e[rng() % sizeof e]; }
		default: return static_cast<unsigned char>(rng());
	}
}

template <typename Scan, typename Ref>
static void check_hits(const Scan& scan, const Ref& in_class, const std::vector<unsigned char>& set) {
	// Every byte value at every lane of several fillers.
	static const unsigned char fillers[] = {0x00, 0x20, 0x41, 0x7f, 0x80, 0xff};
	for (unsigned char fill : fillers) {
		for (unsigned i = 0; i < simd::width; ++i) {
			for (int v = 0; v < 256; ++v) {
				Bytes blk(simd::width, fill);
				blk[i] = static_cast<unsigned char>(v);
				std::vector<unsigned> want;
				for (unsigned k = 0; k < simd::width; ++k) { if (in_class(blk[k])) { want.push_back(k); } }
				CHECK(lanes(scan.hits(blk.data())) == want);
				CHECK(scan.first(blk.data()) == (want.empty() ? simd::width : want[0]));
			}
		}
	}
	// Random blocks drawn from interesting bytes: the whole mask, not only the first hit.
	for (int r = 0; r < 20000; ++r) {
		Bytes blk(simd::width);
		for (auto& b : blk) { b = pick(set); }
		std::vector<unsigned> want;
		for (unsigned k = 0; k < simd::width; ++k) { if (in_class(blk[k])) { want.push_back(k); } }
		CHECK(lanes(scan.hits(blk.data())) == want);
	}
}

template <typename Scan, typename Ref>
static void check_find(const Scan& scan, const Ref& in_class, const std::vector<unsigned char>& set) {
	// A buffer whose bytes outside [p, end) are all members/outside bytes: reading past either
	// end would report a hit that is not in the range.
	const unsigned char poison = [&] { for (int v = 0; v < 256; ++v) { if (in_class(static_cast<unsigned char>(v))) { return static_cast<unsigned char>(v); } } return static_cast<unsigned char>(0); }();
	unsigned char clean = 'a';
	for (int v = 0x61; v < 0x7f; ++v) { if (!in_class(static_cast<unsigned char>(v))) { clean = static_cast<unsigned char>(v); break; } }
	if (in_class(clean)) { return; }  // no clean byte to build a miss from
	for (std::size_t len = 0; len <= 3 * simd::width + 5; ++len) {
		for (std::size_t off = 0; off < 2 * simd::width; ++off) {
			Bytes buf(off + len + 2 * simd::width, poison);
			for (std::size_t i = 0; i < len; ++i) { buf[off + i] = clean; }
			const char* p = reinterpret_cast<const char*>(buf.data()) + off;
			const char* end = p + len;
			CHECK(scan.find(p, end) == end);
			for (std::size_t pos = 0; pos < len; ++pos) {
				buf[off + pos] = poison;
				CHECK(scan.find(p, end) == p + pos);
				// A second hit after the first changes nothing.
				if (pos + 1 < len) {
					buf[off + pos + 1 + rng() % (len - pos - 1)] = poison;
					CHECK(scan.find(p, end) == p + pos);
				}
				for (std::size_t i = 0; i < len; ++i) { buf[off + i] = clean; }
			}
			// Random contents, compared with a scalar scan.
			for (int r = 0; r < 4; ++r) {
				for (std::size_t i = 0; i < len; ++i) { buf[off + i] = (rng() % 6) ? clean : pick(set); }
				const char* want = end;
				for (const char* q = p; q != end; ++q) { if (in_class(static_cast<unsigned char>(*q))) { want = q; break; } }
				CHECK(scan.find(p, end) == want);
			}
		}
	}
}

template <std::size_t N>
static auto make_first_of(const std::array<unsigned char, N>& a) {
	return std::apply([](auto... c) { return simd::FirstOf<sizeof...(c)>(c...); }, a);
}
template <std::size_t N>
static auto make_outside(unsigned lo, unsigned hi, const std::array<unsigned char, N>& a) {
	return std::apply([&](auto... c) { return simd::Outside<sizeof...(c)>(lo, hi, c...); }, a);
}

template <std::size_t N>
static void test_first_of() {
	for (int round = 0; round < (N == 5 ? 12 : 6); ++round) {
		std::array<unsigned char, N> a;
		for (auto& c : a) { c = pick({}); }
		if (round == 0) { for (std::size_t i = 0; i < N; ++i) { a[i] = static_cast<unsigned char>(i); } }          // 0, 1, 2...: the SWAR trouble spots
		if (round == 1) { for (std::size_t i = 0; i < N; ++i) { a[i] = static_cast<unsigned char>(0x80 + i); } }  // high bit
		const auto f = make_first_of(a);
		std::vector<unsigned char> set(a.begin(), a.end());
		auto in_class = [&](unsigned char c) { for (auto s : set) { if (c == s) { return true; } } return false; };
		check_hits(f, in_class, set);
		check_find(f, in_class, set);
	}
}

template <std::size_t N>
static void test_outside() {
	static const unsigned ranges[][2] = {{0x20, 0x7e}, {0x00, 0x7f}, {0x30, 0x39}, {0x7f, 0x7f}, {0x00, 0x00}, {0x41, 0x5a}, {0x01, 0x7e}, {0x00, 0x7e}, {0x21, 0x7f}};
	for (const auto& r : ranges) {
		for (int round = 0; round < (N == 0 ? 1 : 3); ++round) {
			std::array<unsigned char, N> a;
			for (auto& c : a) { c = pick({}); }
			if (round == 0 && N >= 2) { a[0] = '\\'; a[1] = '\''; }
			const auto o = make_outside(r[0], r[1], a);
			std::vector<unsigned char> set(a.begin(), a.end());
			const unsigned lo = r[0], hi = r[1];
			auto in_class = [&](unsigned char c) {
				if (c < lo || c > hi) { return true; }
				for (auto s : set) { if (c == s) { return true; } }
				return false;
			};
			check_hits(o, in_class, set);
			check_find(o, in_class, set);
		}
	}
}

static void test_find_outside() {
	for (int r = 0; r < 2000; ++r) {
		const std::size_t len = rng() % 100;
		Bytes buf(len + 40);
		for (auto& b : buf) { b = (rng() % 8) ? static_cast<unsigned char>(0x30 + rng() % 10) : pick({}); }
		const char* p = reinterpret_cast<const char*>(buf.data()) + rng() % 20;
		const char* end = p + len;
		const char* want = end;
		for (const char* q = p; q != end; ++q) {
			const unsigned char c = static_cast<unsigned char>(*q);
			if (c < '0' || c > '9') { want = q; break; }
		}
		CHECK(simd::find_outside(p, end, '0', '9') == want);
	}
}

static unsigned char ref_lower(unsigned char c) { return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + 32) : c; }
static unsigned char ref_upper(unsigned char c) { return c >= 'a' && c <= 'z' ? static_cast<unsigned char>(c - 32) : c; }

static void test_case_fold() {
	// Every byte value at every position of every short length, out of place and in place.
	for (std::size_t len = 0; len <= 3 * simd::width + 3; ++len) {
		for (std::size_t pos = 0; pos < (len ? len : 1); ++pos) {
			for (int v = 0; v < 256; ++v) {
				Bytes src(len, 'M');
				if (len) { src[pos] = static_cast<unsigned char>(v); }
				for (int upper = 0; upper < 2; ++upper) {
					Bytes want(src);
					for (auto& c : want) { c = upper ? ref_upper(c) : ref_lower(c); }
					Bytes dst(len + 16, 0xa5);
					auto* d = reinterpret_cast<char*>(dst.data()) + 8;
					const auto* s = reinterpret_cast<const char*>(src.data());
					(upper ? simd::ascii_upper : simd::ascii_lower)(s, len, d);
					for (std::size_t i = 0; i < len; ++i) { CHECK(dst[8 + i] == want[i]); }
					for (std::size_t i = 0; i < 8; ++i) { CHECK(dst[i] == 0xa5); }                  // nothing before
					for (std::size_t i = 0; i < 8; ++i) { CHECK(dst[8 + len + i] == 0xa5); }        // nothing after
					// In place.
					Bytes inplace(len + 16, 0xa5);
					for (std::size_t i = 0; i < len; ++i) { inplace[8 + i] = src[i]; }
					auto* ip = reinterpret_cast<char*>(inplace.data()) + 8;
					(upper ? simd::ascii_upper : simd::ascii_lower)(ip, len, ip);
					for (std::size_t i = 0; i < len; ++i) { CHECK(inplace[8 + i] == want[i]); }
					for (std::size_t i = 0; i < 8; ++i) { CHECK(inplace[i] == 0xa5 && inplace[8 + len + i] == 0xa5); }
				}
			}
		}
	}
	// Long random text.
	for (int r = 0; r < 200; ++r) {
		Bytes src(rng() % 600);
		for (auto& c : src) { c = static_cast<unsigned char>(rng() % 3 ? 0x40 + rng() % 64 : rng()); }
		Bytes lo(src.size()), up(src.size());
		simd::ascii_lower(reinterpret_cast<const char*>(src.data()), src.size(), reinterpret_cast<char*>(lo.data()));
		simd::ascii_upper(reinterpret_cast<const char*>(src.data()), src.size(), reinterpret_cast<char*>(up.data()));
		for (std::size_t i = 0; i < src.size(); ++i) { CHECK(lo[i] == ref_lower(src[i]) && up[i] == ref_upper(src[i])); }
	}
}

int main() {
	test_backend();
	test_mask_ops();
	test_first_of<1>();
	test_first_of<2>();
	test_first_of<3>();
	test_first_of<5>();
	test_outside<0>();
	test_outside<1>();
	test_outside<2>();
	test_outside<3>();
	test_find_outside();
	test_case_fold();
	std::printf("all chars_simd tests passed (%s)\n", simd::backend);
	return 0;
}
