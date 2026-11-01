// Tests for chars_escape.hh, the escaping kernel shared by escape() and repr().
//
// The oracle is a scalar escaper written here from the rules in the header (not from the
// tables): measure and emit must agree with it on every byte at every position of every short
// length and on random mixes, in every quote mode, and emit must not write outside the
// measured room. Built twice, like simd_test.cc: with the target's backend and with
// CHARS_NO_SIMD.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "chars_escape.hh"
#include "guard_page.hh"
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

static std::mt19937_64 rng(777);

// The rules, spelled out.
static std::string ref_escape(const std::string& s, char quote) {
	static const char* hex = "0123456789abcdef";
	std::string out;
	for (char ch : s) {
		const unsigned char c = static_cast<unsigned char>(ch);
		if (c == '\n') { out += "\\n"; }
		else if (c == '\r') { out += "\\r"; }
		else if (c == '\t') { out += "\\t"; }
		else if (c == '\\') { out += "\\\\"; }
		else if (quote != '\0' && ch == quote) { out += '\\'; out += ch; }
		else if (c < 0x20 || c >= 0x7f) { out += "\\x"; out += hex[c >> 4]; out += hex[c & 15]; }
		else { out += ch; }
	}
	return out;
}

static const char kQuotes[] = {'\0', '\'', '"', 'A', '\x7f', '\x80', '\\', '\n', '/'};

// measure() and emit() of `s` against the oracle; emit gets exactly the measured room (so an
// overrun is a sanitizer report) and, in a second run, a larger one whose slack must stay untouched.
static void check_one(const std::string& s, char quote) {
	chars::EscapeTable scratch;
	const chars::EscapeTable& t = chars::escape_table(quote, scratch);
	const auto* b = reinterpret_cast<const unsigned char*>(s.data());
	const auto* e = b + s.size();
	const std::string want = ref_escape(s, quote);
	CHECK(chars::escape_measure(b, e, t) == want.size());

	{
		std::unique_ptr<char[]> out(new char[want.size() + 1]);
		char* end = chars::escape_emit(out.get(), b, e, t);
		CHECK(static_cast<std::size_t>(end - out.get()) == want.size());
		CHECK(std::memcmp(out.get(), want.data(), want.size()) == 0);
	}
	{
		std::vector<char> out(want.size() + 40, '\xa5');
		char* end = chars::escape_emit(out.data() + 8, b, e, t);
		CHECK(static_cast<std::size_t>(end - (out.data() + 8)) == want.size());
		CHECK(std::memcmp(out.data() + 8, want.data(), want.size()) == 0);
		for (int i = 0; i < 8; ++i) { CHECK(out[i] == '\xa5'); }
		for (std::size_t i = 8 + want.size(); i < out.size(); ++i) { CHECK(out[i] == '\xa5'); }
	}
}

static void test_tables() {
	for (char quote : kQuotes) {
		chars::EscapeTable scratch;
		const chars::EscapeTable& t = chars::escape_table(quote, scratch);
		CHECK(t.quote == quote);
		CHECK(t.quote8 == 0x0101010101010101ULL * static_cast<unsigned char>(quote));
		for (int c = 0; c < 256; ++c) {
			const std::string one(1, static_cast<char>(c));
			const std::string want = ref_escape(one, quote);
			CHECK(t.len[c] == want.size());
			CHECK(std::memcmp(t.out[c], want.data(), want.size()) == 0);
			// The byte writer (which uses chars::char_repr for the hex) agrees with the table.
			char buf[8];
			char* end = chars::escape_byte(buf, static_cast<unsigned char>(c), t);
			CHECK(static_cast<std::size_t>(end - buf) == want.size() && std::memcmp(buf, want.data(), want.size()) == 0);
		}
	}
	// The shared tables are the three common modes.
	CHECK(chars::escape_table_none.quote == '\0' && chars::escape_table_single.quote == '\'' && chars::escape_table_double.quote == '"');
	static_assert(chars::make_escape_table('"').len['"'] == 2, "built at compile time");
}

static void test_every_byte_everywhere() {
	// Every byte value at every position of every length across the block sizes (and into the
	// 8-byte and one-byte tails), over a printable filler and a dirty one.
	for (char quote : kQuotes) {
		for (std::size_t len = 0; len <= 70; ++len) {
			for (std::size_t pos = 0; pos < len; ++pos) {
				for (int v = 0; v < 256; ++v) {
					std::string s(len, 'a');
					s[pos] = static_cast<char>(v);
					check_one(s, quote);
				}
			}
		}
	}
	for (std::size_t len = 0; len <= 40; ++len) {
		for (int v = 0; v < 256; ++v) {
			std::string s(len, static_cast<char>(v));  // the whole input one byte
			for (char quote : kQuotes) { check_one(s, quote); }
		}
	}
}

static void test_random() {
	static const int dirty_pct[] = {0, 1, 3, 10, 30, 50, 100};
	for (int pct : dirty_pct) {
		for (int r = 0; r < 1500; ++r) {
			const std::size_t len = (r % 7 == 0) ? 1000 + rng() % 3000 : rng() % 200;
			std::string s(len, 'a');
			for (auto& c : s) {
				c = (static_cast<int>(rng() % 100) < pct) ? static_cast<char>(rng()) : static_cast<char>(0x20 + rng() % 95);
			}
			// Make the quote and the backslash common among the printable bytes too.
			if (r % 3 == 0) { for (auto& c : s) { if (rng() % 9 == 0) { c = "'\"\\\n\r\t\0"[rng() % 7]; } } }
			check_one(s, kQuotes[rng() % (sizeof kQuotes)]);
		}
	}
}

#ifdef CHARS_TEST_GUARD_PAGES
// The measured bytes are the room the writer gets and the input bytes are all it may read. Against an
// unmapped page, one byte more or less than that is a crash, in any build: the output ends exactly at
// the page, and the input ends exactly at a page and then starts exactly at one.
static void check_guarded(const std::string& s, char quote, GuardedRegion& in_region, GuardedRegion& out_region) {
	chars::EscapeTable scratch;
	const chars::EscapeTable& t = chars::escape_table(quote, scratch);
	const std::string want = ref_escape(s, quote);
	for (int side = 0; side < 2; ++side) {
		char* in = side == 0 ? in_region.at_end(s.size()) : in_region.at_start();
		std::memcpy(in, s.data(), s.size());
		const auto* b = reinterpret_cast<const unsigned char*>(in);
		const auto* e = b + s.size();
		CHECK(chars::escape_measure(b, e, t) == want.size());
		char* out = out_region.at_end(want.size());
		char* end = chars::escape_emit(out, b, e, t);
		CHECK(end == out + want.size());
		CHECK(std::memcmp(out, want.data(), want.size()) == 0);
	}
}

static void test_no_overrun() {
	GuardedRegion in_region(1 << 14), out_region(1 << 16);
	static const unsigned char interesting[] = {0, '\n', '\\', '\'', '"', ' ', '~', 0x7f, 0x80, 'a'};
	static const char quotes[] = {'\0', '\'', '"', 'A'};
	for (char quote : quotes) {
		for (std::size_t len = 0; len <= 70; ++len) {
			for (std::size_t pos = 0; pos < len; ++pos) {
				for (unsigned char v : interesting) {
					std::string s(len, 'a');
					s[pos] = static_cast<char>(v);
					check_guarded(s, quote, in_region, out_region);
				}
			}
		}
	}
	for (int r = 0; r < 3000; ++r) {
		const int pct = static_cast<int>(rng() % 101);
		std::string s(rng() % 300, 'a');
		for (auto& c : s) { c = (static_cast<int>(rng() % 100) < pct) ? static_cast<char>(rng()) : static_cast<char>(0x20 + rng() % 95); }
		check_guarded(s, kQuotes[rng() % (sizeof kQuotes)], in_region, out_region);
	}
}
#endif

int main() {
	std::printf("chars_escape: backend %s\n", chars::simd::backend);
	test_tables();
	test_every_byte_everywhere();
	test_random();
#ifdef CHARS_TEST_GUARD_PAGES
	test_no_overrun();
#endif
	std::printf("all chars_escape tests passed (%s)\n", chars::simd::backend);
	return 0;
}
