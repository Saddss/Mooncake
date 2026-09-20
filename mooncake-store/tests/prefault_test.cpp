// prefault_test.cpp
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstddef>
#include <vector>

#include "common/client_buffer_allocation.h"

namespace mooncake {
namespace {

// mincore() reports residency in the least significant bit of each byte.
bool all_pages_resident(void* addr, size_t size) {
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return false;
    }
    const size_t page_count = (size + page_size - 1) / page_size;
    std::vector<unsigned char> residency(page_count, 0);
    if (mincore(addr, size, residency.data()) != 0) {
        return false;
    }
    for (unsigned char page : residency) {
        if ((page & 1) == 0) {
            return false;
        }
    }
    return true;
}

void* map_anonymous(size_t size) {
    return mmap(nullptr, size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

}  // namespace

TEST(PrefaultForPinningTest, PopulatesAFreshAnonymousMapping) {
    constexpr size_t kSize = 16 * 1024 * 1024;
    void* region = map_anonymous(kSize);
    ASSERT_NE(region, MAP_FAILED);
    ASSERT_FALSE(all_pages_resident(region, kSize));

    prefault_for_pinning(region, kSize);

    EXPECT_TRUE(all_pages_resident(region, kSize));
    EXPECT_EQ(munmap(region, kSize), 0);
}

TEST(PrefaultForPinningTest, PopulatesEveryPageOfALargeRange) {
    // 128 MiB, checked page by page, so a helper that only touches the head of
    // the range fails here.
    constexpr size_t kSize = 128 * 1024 * 1024;
    void* region = map_anonymous(kSize);
    ASSERT_NE(region, MAP_FAILED);

    prefault_for_pinning(region, kSize);

    EXPECT_TRUE(all_pages_resident(region, kSize));
    EXPECT_EQ(munmap(region, kSize), 0);
}

TEST(PrefaultForPinningTest, StaysResidentOnRepeatCalls) {
    constexpr size_t kSize = 8 * 1024 * 1024;
    void* region = map_anonymous(kSize);
    ASSERT_NE(region, MAP_FAILED);

    prefault_for_pinning(region, kSize);
    ASSERT_TRUE(all_pages_resident(region, kSize));
    prefault_for_pinning(region, kSize);

    EXPECT_TRUE(all_pages_resident(region, kSize));
    EXPECT_EQ(munmap(region, kSize), 0);
}

TEST(PrefaultForPinningTest, SkipsAddressesOutsideTheProcess) {
    // Accelerator memory (for example a cudaMalloc'd VRAM segment) is not part
    // of the process address space, and a CPU store to it faults.  The helper
    // probes the mapping first, so an unmapped address is a no-op instead.
    prefault_for_pinning(reinterpret_cast<void*>(0x1000), 4096);
    SUCCEED();
}

TEST(PrefaultForPinningTest, ToleratesEmptyAndNullRanges) {
    prefault_for_pinning(nullptr, 4096);
    prefault_for_pinning(nullptr, 0);

    constexpr size_t kSize = 4 * 4096;
    void* region = map_anonymous(kSize);
    ASSERT_NE(region, MAP_FAILED);
    prefault_for_pinning(region, 0);

    EXPECT_EQ(munmap(region, kSize), 0);
}

}  // namespace mooncake
