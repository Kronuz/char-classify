// char-classify comparison benchmark (-DCHAR_CLASSIFY_BUILD_BENCH=ON).
//
// Each row times the library, one comparator and the library again; the spread of the two
// library runs is the noise of the row. Both sides compute the same result, checked before the
// row is timed. Comparators are arithmetic written here, <cctype>, a scalar loop over the
// escape table, and abseil when CMake finds it (HAVE_ABSL).
//
//   char_classify_compare            all rows
//   char_classify_compare hex        only rows whose group or workload contains "hex"
//   char_classify_compare --check    the result checks only, no timing (CTest char_classify_compare)
//
// Times are the best of seven samples of at least 8 ms, in ns per call, or per byte where the
// unit column says so. char_classify_compare_swar is the same program with CHARS_NO_SIMD, the
// portable 8-byte backend, so that a paired run shows what the vector backend buys.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "chars.hh"
#include "chars_escape.hh"
#include "chars_simd.hh"
#ifdef HAVE_ABSL
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#endif

namespace {

using clk = std::chrono::steady_clock;

bool g_check = false;
const char* g_filter = nullptr;
int g_bad = 0;

template <class T>
inline void keep(const T& v) { asm volatile("" : : "r,m"(v) : "memory"); }

template <class F>
double measure(F&& f) {
	std::size_t it = 1;
	for (;;) {
		const auto t0 = clk::now();
		for (std::size_t i = 0; i < it; ++i) { f(); }
		if (std::chrono::duration<double>(clk::now() - t0).count() > 0.008) { break; }
		it *= 2;
	}
	double best = 1e30;
	for (int k = 0; k < 7; ++k) {
		const auto t0 = clk::now();
		for (std::size_t i = 0; i < it; ++i) { f(); }
		best = std::min(best, std::chrono::duration<double>(clk::now() - t0).count() / static_cast<double>(it));
	}
	return best * 1e9;
}

// One row: ours, the comparator, ours again. `per` divides the times (bytes per call), `unit` names it.
template <class A, class B>
void row(const char* group, const std::string& what, const char* comparator, bool same, A&& ours, B&& comp,
	double per = 1.0, const char* unit = "call") {
	if (!same) {
		++g_bad;
		std::printf("MISMATCH %s: %s against %s\n", group, what.c_str(), comparator);
	}
	if (g_check) { return; }
	if (g_filter && !std::strstr(group, g_filter) && !std::strstr(what.c_str(), g_filter)) { return; }
	const double o1 = measure(ours);
	const double c = measure(comp);
	const double o2 = measure(ours);
	const double o = std::min(o1, o2);
	std::printf("%-9s %-46s %-5s %8.3f  %-30s %8.3f  %6.2f  %5.1f%%\n", group, what.c_str(), unit, o / per, comparator,
		c / per, o / c, 100.0 * std::abs(o1 - o2) / o);
	std::fflush(stdout);
}

std::uint64_t rs = 88172645463325252ull;
inline std::uint64_t rnd() {
	rs ^= rs << 13;
	rs ^= rs >> 7;
	rs ^= rs << 17;
	return rs;
}

// Printable ASCII with a few control bytes, quotes and backslashes.
std::string text(std::size_t n) {
	std::string s(n, ' ');
	for (auto& c : s) {
		const std::uint64_t r = rnd() % 100;
		c = r < 2 ? '\n' : r < 3 ? '\t' : r < 4 ? '\'' : r < 5 ? '\\' : static_cast<char>(' ' + rnd() % 95);
	}
	return s;
}

// Printable ASCII with no byte that needs an escape.
std::string plain(std::size_t n) {
	std::string s(n, ' ');
	for (auto& c : s) {
		c = static_cast<char>(0x20 + rnd() % 95);
		if (c == '\'' || c == '\\') { c = 'q'; }
	}
	return s;
}

// Text where one byte in ten is any byte at all.
std::string mixed(std::size_t n) {
	std::string s = text(n);
	for (auto& c : s) {
		if (rnd() % 10 == 0) { c = static_cast<char>(rnd()); }
	}
	return s;
}

std::string hex_text(std::size_t n) {
	std::string s(n, '0');
	for (auto& c : s) { c = "0123456789abcdefABCDEF"[rnd() % 22]; }
	return s;
}

template <class P>
std::size_t count(const std::string& s, P p) {
	std::size_t k = 0;
	for (const char c : s) { k += p(c) ? 1 : 0; }
	return k;
}

// ---- classification -------------------------------------------------------------------------

void classify() {
	const std::string t = mixed(65536);
	const double n = static_cast<double>(t.size());
	const auto ours_alpha = [&] { return count(t, [](char c) { return chars::is_alpha(c) != 0; }); };
	const auto arith_alpha = [&] { return count(t, [](char c) { return static_cast<unsigned char>((static_cast<unsigned char>(c) | 0x20) - 'a') < 26; }); };
	const auto std_alpha = [&] { return count(t, [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }); };
	row("classify", "is_alpha count, 64 KB", "(c | 32) - 'a' < 26", ours_alpha() == arith_alpha(), [&] { keep(ours_alpha()); },
		[&] { keep(arith_alpha()); }, n, "byte");
	row("classify", "is_alpha count, 64 KB", "std::isalpha", ours_alpha() == std_alpha(), [&] { keep(ours_alpha()); },
		[&] { keep(std_alpha()); }, n, "byte");
	const auto ours_hex = [&] { return count(t, [](char c) { return chars::is_hexdigit(c) != 0; }); };
	const auto arith_hex = [&] {
		return count(t, [](char c) {
			const unsigned char u = static_cast<unsigned char>(c);
			return u - '0' < 10u || (u | 0x20) - 'a' < 6u;
		});
	};
	row("classify", "is_hexdigit count, 64 KB", "arithmetic ranges", ours_hex() == arith_hex(), [&] { keep(ours_hex()); },
		[&] { keep(arith_hex()); }, n, "byte");
#ifdef HAVE_ABSL
	const auto absl_alpha = [&] { return count(t, [](char c) { return absl::ascii_isalpha(static_cast<unsigned char>(c)); }); };
	const auto absl_hex = [&] { return count(t, [](char c) { return absl::ascii_isxdigit(static_cast<unsigned char>(c)); }); };
	row("classify", "is_alpha count, 64 KB", "absl::ascii_isalpha", ours_alpha() == absl_alpha(), [&] { keep(ours_alpha()); },
		[&] { keep(absl_alpha()); }, n, "byte");
	row("classify", "is_hexdigit count, 64 KB", "absl::ascii_isxdigit", ours_hex() == absl_hex(), [&] { keep(ours_hex()); },
		[&] { keep(absl_hex()); }, n, "byte");
#endif
}

// ---- hex ------------------------------------------------------------------------------------

void hex() {
	// Encode: a byte to two lowercase digits.
	for (const std::size_t n : {16, 256, 65536}) {
		std::string bytes(n, '\0');
		for (auto& c : bytes) { c = static_cast<char>(rnd()); }
		std::string out(n * 2, '\0');
		std::string ref(n * 2, '\0');
		const auto ours = [&] {
			char* d = out.data();
			for (const char c : bytes) { chars::char_repr(c, &d); }
			keep(out);
		};
		const auto table = [&] {
			char* d = ref.data();
			for (const char c : bytes) {
				*d++ = "0123456789abcdef"[static_cast<unsigned char>(c) >> 4];
				*d++ = "0123456789abcdef"[static_cast<unsigned char>(c) & 15];
			}
			keep(ref);
		};
		ours();
		table();
		const std::string what = "char_repr hex-encode, " + std::to_string(n) + " B";
		row("hex", what, "nibble string lookup", out == ref, ours, table, static_cast<double>(n), "byte");
#ifdef HAVE_ABSL
		row("hex", what, "absl::BytesToHexString", out == absl::BytesToHexString(bytes), ours,
			[&] { keep(absl::BytesToHexString(bytes)); }, static_cast<double>(n), "byte");
#endif
	}

	// Decode: pairs of hex digits to bytes, into a buffer.
	const std::string digits = hex_text(8192);
	const std::size_t pairs = digits.size() / 2;
	std::string a(pairs, '\0');
	std::string b(pairs, '\0');
	std::string c(pairs, '\0');
	const auto nibble = [](char ch) -> int { return ch <= '9' ? ch - '0' : (ch | 0x20) - 'a' + 10; };
	const auto by_hexdec = [&] {
		const char* p = digits.data();
		for (auto& o : a) { o = static_cast<char>(chars::hexdec(&p)); }
		keep(a);
	};
	const auto by_value = [&] {
		for (std::size_t i = 0; i < pairs; ++i) {
			b[i] = static_cast<char>(chars::hex_value(digits[2 * i]) << 4 | chars::hex_value(digits[2 * i + 1]));
		}
		keep(b);
	};
	const auto by_arith = [&] {
		for (std::size_t i = 0; i < pairs; ++i) { c[i] = static_cast<char>(nibble(digits[2 * i]) << 4 | nibble(digits[2 * i + 1])); }
		keep(c);
	};
	by_hexdec();
	by_value();
	by_arith();
	const double bytes = static_cast<double>(pairs);
	row("hex", "hexdec decode, 4096 B", "arithmetic nibbles", a == c, by_hexdec, by_arith, bytes, "byte");
	row("hex", "hex_value decode, 4096 B", "arithmetic nibbles", b == c, by_value, by_arith, bytes, "byte");
#ifdef HAVE_ABSL
	std::string unhexed;
	const bool decoded = absl::HexStringToBytes(digits, &unhexed);
	row("hex", "hex_value decode, 4096 B", "absl::HexStringToBytes", decoded && unhexed == b, by_value,
		[&] { std::string o; (void)absl::HexStringToBytes(digits, &o); keep(o); }, bytes, "byte");
#endif
}

// ---- case folding ---------------------------------------------------------------------------

// The comparators take plain pointers marked __restrict: through a std::string the compiler cannot prove that the source and
// the destination do not overlap and leaves the loop scalar, which would be a straw man. A noinline function keeps the loop
// from being folded into the timing lambda.
CHARS_NOINLINE void arith_lower(const char* __restrict s, std::size_t n, char* __restrict d) {
	for (std::size_t i = 0; i < n; ++i) { d[i] = (s[i] >= 'A' && s[i] <= 'Z') ? static_cast<char>(s[i] + 32) : s[i]; }
}

CHARS_NOINLINE void table_lower(const char* __restrict s, std::size_t n, char* __restrict d) {
	for (std::size_t i = 0; i < n; ++i) { d[i] = chars::tolower(s[i]); }
}

void fold() {
	for (const std::size_t n : {16, 256, 65536}) {
		const std::string src = text(n);
		std::string a(n, '\0');
		std::string b(n, '\0');
		const auto vec = [&] { chars::simd::ascii_lower(src.data(), n, a.data()); keep(a); };
		const auto arith = [&] { arith_lower(src.data(), n, b.data()); keep(b); };
		const auto table = [&] { table_lower(src.data(), n, b.data()); keep(b); };
		vec();
		arith();
		const std::string what = "ascii_lower, " + std::to_string(n) + " B";
		row("fold", what, "arithmetic loop (__restrict)", a == b, vec, arith, static_cast<double>(n), "byte");
		table();
		row("fold", what, "chars::tolower per byte", a == b, vec, table, static_cast<double>(n), "byte");
#ifdef HAVE_ABSL
		std::string d;
		const auto fold_copy = [&] { d.assign(src); absl::AsciiStrToLower(&d); keep(d); };
		fold_copy();
		row("fold", what, "absl::AsciiStrToLower (copy)", a == d, vec, fold_copy, static_cast<double>(n), "byte");
#endif
	}
}

// ---- scanning -------------------------------------------------------------------------------

void scan() {
	for (const std::size_t n : {16, 256, 4096}) {
		for (const int where : {0, 1}) {  // 0: no match, 1: a match in the middle
			std::string buf(n, 'a');
			if (where) { buf[n / 2] = ','; buf[n / 2 + 1] = '='; }
			const char* const p = buf.data();
			const char* const e = p + n;
			const std::string tag = ", " + std::to_string(n) + " B, " + (where ? "hit at half" : "no match");

			const chars::simd::FirstOf<1> comma(',');
			const char* const m = static_cast<const char*>(std::memchr(p, ',', n));
			row("scan", "FirstOf<1>" + tag, "memchr", comma.find(p, e) == (m ? m : e), [&] { keep(comma.find(p, e)); },
				[&] { keep(std::memchr(p, ',', n)); });

			const chars::simd::FirstOf<2> two(',', '=');
			const std::string_view sv(p, n);
			const std::size_t at = sv.find_first_of(",=");
			const char* const want = at == std::string_view::npos ? e : p + at;
			const auto ours = [&] { keep(two.find(p, e)); };
			row("scan", "FirstOf<2>" + tag, "string_view::find_first_of", two.find(p, e) == want, ours,
				[&] { keep(sv.find_first_of(",=")); });
			row("scan", "FirstOf<2>" + tag, "byte loop", two.find(p, e) == want, ours, [&] {
				const char* q = p;
				while (q != e && *q != ',' && *q != '=') { ++q; }
				keep(q);
			});
		}
		std::string digits(n, '7');
		digits[n - 1] = 'x';
		const char* const p = digits.data();
		const char* const e = p + n;
		row("scan", "find_outside digits, " + std::to_string(n) + " B, last byte", "std::find_if",
			chars::simd::find_outside(p, e, '0', '9') == p + n - 1, [&] { keep(chars::simd::find_outside(p, e, '0', '9')); },
			[&] { keep(std::find_if(p, e, [](char ch) { return ch < '0' || ch > '9'; })); });
	}
}

// ---- the escaping kernel --------------------------------------------------------------------

// Two passes over the table, a byte at a time: the cost the block tests avoid.
std::size_t scalar_escape(const unsigned char* b, const unsigned char* e, const chars::EscapeTable& t, char* dst) {
	std::size_t n = 0;
	for (const unsigned char* q = b; q != e; ++q) { n += t.len[*q]; }
	char* d = dst;
	for (; b != e; ++b) {
		std::memcpy(d, t.out[*b], 4);
		d += t.len[*b];
	}
	return n;
}

void kernel() {
	const chars::EscapeTable& t = chars::escape_table_single;
	struct Case { const char* name; std::string s; };
	const Case cases[] = {
		{"printable, 4 KB", plain(4096)},
		{"text with escapes, 4 KB", text(4096)},
		{"mixed 10%, 64 KB", mixed(65536)},
	};
	for (const auto& k : cases) {
		const auto* b = reinterpret_cast<const unsigned char*>(k.s.data());
		const auto* e = b + k.s.size();
		std::string a(k.s.size() * 4 + 64, '\0');
		std::string r(k.s.size() * 4 + 64, '\0');
		const auto ours = [&] {
			const std::size_t m = chars::escape_measure(b, e, t);
			char* const d = chars::escape_emit(a.data(), b, e, t);
			keep(m);
			keep(d);
		};
		const auto scalar = [&] { keep(scalar_escape(b, e, t, r.data())); };
		const std::size_t m = chars::escape_measure(b, e, t);
		char* const d = chars::escape_emit(a.data(), b, e, t);
		const std::size_t want = scalar_escape(b, e, t, r.data());
		const bool same = static_cast<std::size_t>(d - a.data()) == m && m == want && std::memcmp(a.data(), r.data(), m) == 0;
		row("kernel", std::string("escape_measure + escape_emit, ") + k.name, "scalar table loop, 2 passes", same, ours, scalar,
			static_cast<double>(k.s.size()), "byte");
	}
}

}  // namespace

int main(int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--check") == 0) { g_check = true; }
		else { g_filter = argv[i]; }
	}
	if (!g_check) {
		std::printf("# backend %s, %zu-byte blocks\n", chars::simd::backend, chars::simd::width);
#ifdef HAVE_ABSL
		std::printf("# abseil comparators: yes\n");
#else
		std::printf("# abseil comparators: no\n");
#endif
		std::printf("%-9s %-46s %-5s %8s  %-30s %8s  %6s  %6s\n", "group", "workload", "unit", "ours", "comparator", "comp", "ratio", "A/A");
	}
	classify();
	hex();
	fold();
	scan();
	kernel();
	if (g_bad) {
		std::fprintf(stderr, "%d rows disagree with their comparator\n", g_bad);
		return 1;
	}
	if (g_check) { std::printf("all char-classify comparison checks passed (%s)\n", chars::simd::backend); }
	return 0;
}
