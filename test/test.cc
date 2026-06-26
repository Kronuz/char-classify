// Smoke test for the standalone char-classify library.
//
// Exercises chars.hh: table-driven, constexpr character classification
// (digit/alpha/upper/space/hex), hex-nibble decode, and case folding. Almost
// everything is constexpr, so it is checked with static_assert and runs at
// compile time.
//
// Build via CMake: cmake -B build && cmake --build build && ctest --test-dir build
#include <cassert>
#include <cstdio>
#include <cstring>

#include "chars.hh"


// ---------------------------------------------------------------------------
// Classification predicates: digit / alpha / upper / space / hex.
// ---------------------------------------------------------------------------

static void test_classification() {
	// The is_* predicates return an int (a masked flag), so compare to 0 / cast to
	// bool before combining with && to keep the classification explicit.
	static_assert(chars::is_digit('5') != 0, "5 is a digit");
	static_assert(chars::is_digit('x') == 0, "x is not a digit");

	static_assert(chars::is_alpha('x') != 0 && chars::is_alpha('X') != 0, "x/X are alpha");
	static_assert(chars::is_alpha('5') == 0, "5 is not alpha");

	static_assert(chars::is_upper('X') != 0 && chars::is_upper('x') == 0, "X upper, x not");

	static_assert(chars::is_space(' ') != 0 && chars::is_space('\t') != 0, "space and tab are spaces");
	static_assert(chars::is_space('x') == 0, "x is not a space");

	static_assert(chars::is_alnum('5') != 0 && chars::is_alnum('x') != 0, "5 and x are alnum");
	static_assert(chars::is_alnum('!') == 0, "! is not alnum");

	std::printf("chars classification OK: digit/alpha/upper/space/alnum\n");
}


// ---------------------------------------------------------------------------
// Hex: is_hexdigit, hexdigit nibble value, hexdec two-char decode.
// ---------------------------------------------------------------------------

static void test_hex() {
	static_assert(chars::is_hexdigit('f') != 0 && chars::is_hexdigit('F') != 0 && chars::is_hexdigit('9') != 0,
	              "f/F/9 are hex digits");
	static_assert(chars::is_hexdigit('g') == 0, "g is not a hex digit");

	static_assert(chars::hexdigit('a') == 10, "hexdigit('a') == 10");
	static_assert(chars::hexdigit('F') == 15, "hexdigit('F') == 15");
	static_assert(chars::hexdigit('0') == 0, "hexdigit('0') == 0");

	// Regression: non-hex chars must return the NO_HEXDIGIT sentinel, NOT the
	// in-range-looking 0x1000 (4096) the old IS_NON_HEX flag leaked through the
	// HEX value mask, and not a stray 0 that looks like a valid nibble.
	static_assert(chars::hexdigit('z') == chars::NO_HEXDIGIT, "hexdigit('z') is the sentinel");
	static_assert(chars::hexdigit('g') == chars::NO_HEXDIGIT, "hexdigit('g') is the sentinel");
	static_assert(chars::hexdigit('z') != 4096, "hexdigit('z') is not the stale 0x1000 value");
	static_assert(chars::hexdigit('g') != 0, "hexdigit('g') is not a stray 0 nibble");

	// hexdec decodes two hex chars to a byte and advances the pointer.
	const char* p = "ff";
	assert(chars::hexdec(&p) == 255);
	const char* q = "1b";
	assert(chars::hexdec(&q) == 0x1b);

	// Regression: hexdec must reject invalid input cleanly (return -1, leave the
	// pointer untouched) instead of producing garbage for untrusted input.
	const char* bad1_start = "gg";
	const char* bad1 = bad1_start;
	assert(chars::hexdec(&bad1) == -1 && bad1 == bad1_start);
	const char* bad2_start = "0x";  // first nibble valid, second isn't
	const char* bad2 = bad2_start;
	assert(chars::hexdec(&bad2) == -1 && bad2 == bad2_start);

	// Valid decodes still work and still advance the pointer past both nibbles.
	const char* ok1 = "ff";
	assert(chars::hexdec(&ok1) == 255 && *ok1 == '\0');
	const char* ok2 = "0a";
	assert(chars::hexdec(&ok2) == 10 && *ok2 == '\0');

	std::printf("chars hex OK: is_hexdigit, hexdigit nibble, hexdec\n");
}


// ---------------------------------------------------------------------------
// Case folding: tolower / toupper (full byte, no locale).
// ---------------------------------------------------------------------------

static void test_case_fold() {
	static_assert(chars::tolower('A') == 'a', "tolower('A') == 'a'");
	static_assert(chars::tolower('z') == 'z', "tolower leaves lowercase alone");
	static_assert(chars::toupper('a') == 'A', "toupper('a') == 'A'");
	static_assert(chars::toupper('Z') == 'Z', "toupper leaves uppercase alone");

	// Non-letters are unchanged by the fold.
	static_assert(chars::tolower('5') == '5', "tolower leaves digits alone");
	static_assert(chars::toupper('-') == '-', "toupper leaves punctuation alone");

	// char_repr writes the two-char lowercase hex of a byte.
	char buf[3]{};
	char* w = buf;
	chars::char_repr('\x1b', &w);
	assert(std::strcmp(buf, "1b") == 0);

	std::printf("chars case-fold OK: tolower/toupper, char_repr\n");
}


int main() {
	test_classification();
	test_hex();
	test_case_fold();
	std::printf("all char-classify tests passed\n");
	return 0;
}
