// A runnable tour of char-classify.
//
// Build (when this repo is the top-level project):
//   cmake -B build && cmake --build build && ./build/char_classify_demo
//
// The one idea worth taking away: every classifier here is a constexpr masked
// lookup into one 256-entry table, with no <cctype> and no locale. So the same
// is_digit / is_alpha / hexdigit you call at runtime also folds away inside a
// static_assert. This demo classifies a row of characters, proves the answers
// are compile-time with static_assert, decodes hex (at compile time too), and
// then runs a tiny tokenizer that leans on the predicates to scan an input line.
#include <cstdio>
#include <string_view>

#include "chars.hh"

static void rule(const char* title) {
	std::printf("\n\033[1m── %s ──\033[0m\n", title);
}

// A compile-time hex decode, to show hexdec runs in constexpr context. It takes
// a char** and advances it; here we hand it a pointer into a constexpr literal.
constexpr int hex_byte(const char* two) {
	const char* p = two;
	return chars::hexdec(&p);
}

// A minimal token kind for the scan below.
enum class kind { word, number, space, other };

int main() {
	std::puts("char-classify demo");

	// --- 1. classify a row of characters -------------------------------------
	rule("each is_* is a masked table lookup (note: returns int, not bool)");
	// The predicates return the masked flag bits, so a true result is some
	// non-zero int, not necessarily 1. The table answers digit/alpha/upper/
	// space/hex without ever touching <cctype>.
	std::puts("  char  digit alpha upper space hex");
	for (char c : std::string_view("7 aZ_x")) {
		char shown = (c == ' ') ? '.' : c;  // show the space as a dot
		std::printf("   %c     %d     %d     %d     %d    %d\n",
		            shown,
		            chars::is_digit(c) != 0, chars::is_alpha(c) != 0,
		            chars::is_upper(c) != 0, chars::is_space(c) != 0,
		            chars::is_hexdigit(c) != 0);
	}

	// --- 2. proof it really happened at compile time -------------------------
	rule("static_assert: the classification ran in the compiler");
	// These are not runtime checks: the table lookup folds to a constant the
	// compiler can assert on. If chars.hh were not constexpr this would not
	// compile.
	static_assert(chars::is_digit('5') != 0, "5 is a digit");
	static_assert(chars::is_alpha('x') != 0, "x is alpha");
	static_assert(chars::is_upper('X') != 0 && chars::is_upper('x') == 0, "X upper, x not");
	static_assert(chars::tolower('A') == 'a' && chars::toupper('a') == 'A', "case fold, no locale");
	std::puts("  digit/alpha/upper and tolower/toupper all held at compile time");

	// --- 3. hex, decoded at compile time -------------------------------------
	rule("hexdigit() nibbles and hexdec() bytes (constexpr-clean)");
	// hexdigit returns the nibble value; hexdec decodes two of them to a byte and
	// only advances the pointer on success, so bad input is rejected cleanly.
	static_assert(chars::hexdigit('a') == 10, "hexdigit('a') == 10");
	static_assert(chars::hexdigit('z') == chars::NO_HEXDIGIT, "non-hex returns the sentinel");
	static_assert(hex_byte("ff") == 255, "ff -> 255 at compile time");
	static_assert(hex_byte("1b") == 0x1b, "1b -> 27 at compile time");
	static_assert(hex_byte("gg") == -1, "invalid input rejected, not garbage");
	std::printf("  hexdec(\"ff\") = %d, hexdec(\"1b\") = %d, hexdec(\"gg\") = %d  (all folded at compile time)\n",
	            hex_byte("ff"), hex_byte("1b"), hex_byte("gg"));

	// --- 4. a tiny tokenizer driven by the predicates ------------------------
	rule("a scan that leans on the predicates");
	// Walk a line, grouping runs of alnum into words/numbers and runs of space
	// into whitespace, the way a lexer's inner loop does. is_alnum/is_digit/
	// is_space carry the whole decision; there is no <cctype> anywhere.
	std::string_view line = "x42 = ab + 7;";
	std::printf("  input: \"%.*s\"\n", (int)line.size(), line.data());
	std::size_t i = 0;
	while (i < line.size()) {
		std::size_t start = i;
		kind k;
		if (chars::is_space(line[i])) {
			while (i < line.size() && chars::is_space(line[i])) ++i;
			k = kind::space;
		} else if (chars::is_alnum(line[i])) {
			// A run starting with a digit is a number; otherwise a word.
			bool numeric = chars::is_digit(line[i]) != 0;
			while (i < line.size() && chars::is_alnum(line[i])) {
				if (!chars::is_digit(line[i])) numeric = false;
				++i;
			}
			k = numeric ? kind::number : kind::word;
		} else {
			++i;
			k = kind::other;
		}
		std::string_view tok = line.substr(start, i - start);
		const char* label =
			k == kind::word   ? "word  " :
			k == kind::number ? "number" :
			k == kind::space  ? "space " : "other ";
		if (k == kind::space) {
			std::printf("    %s [%zu space(s)]\n", label, tok.size());
		} else {
			std::printf("    %s \"%.*s\"\n", label, (int)tok.size(), tok.data());
		}
	}

	std::puts("\ndone.");
	return 0;
}
