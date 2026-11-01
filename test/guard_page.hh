// A buffer that ends, or starts, exactly at an unmapped page: a read or write one byte outside it faults.
//
// The block scanners and the escaping kernel promise to touch only the bytes they are given (and, for
// the writer, only the bytes measured). A sanitizer finds a violation; this finds it without one, as a
// crash, in every build the tests run in. POSIX only (mmap); on other systems the tests that use it
// compile to nothing.
#pragma once

#if defined(__unix__) || defined(__APPLE__)
#define CHARS_TEST_GUARD_PAGES 1

#include <sys/mman.h>
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>

class GuardedRegion {
	char* base_ = nullptr;
	std::size_t page_ = 0;
	std::size_t data_pages_ = 0;

public:
	// Room for `capacity` bytes between two unmapped pages.
	explicit GuardedRegion(std::size_t capacity) {
		page_ = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
		data_pages_ = (capacity + page_ - 1) / page_;
		const std::size_t total = (data_pages_ + 2) * page_;
		void* p = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (p == MAP_FAILED) {
			std::fprintf(stderr, "mmap failed\n");
			std::exit(2);
		}
		base_ = static_cast<char*>(p);
		if (mprotect(base_, page_, PROT_NONE) != 0 || mprotect(base_ + (data_pages_ + 1) * page_, page_, PROT_NONE) != 0) {
			std::fprintf(stderr, "mprotect failed\n");
			std::exit(2);
		}
	}
	GuardedRegion(const GuardedRegion&) = delete;
	GuardedRegion& operator=(const GuardedRegion&) = delete;
	~GuardedRegion() { munmap(base_, (data_pages_ + 2) * page_); }

	// n bytes whose last byte is the last mapped byte: the byte after them is unmapped.
	char* at_end(std::size_t n) const { return base_ + (data_pages_ + 1) * page_ - n; }
	// n bytes whose first byte is the first mapped byte: the byte before them is unmapped.
	char* at_start() const { return base_ + page_; }
};
#endif
