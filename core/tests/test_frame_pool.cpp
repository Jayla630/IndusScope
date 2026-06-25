#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>

#include "indusscope/core/FramePool.h"
#include "indusscope/core/ImageFrame.h"

using namespace indusscope::core;

// (c) Trivially copyable — pass-by-value copies pointer + metadata only.
// (c) 平凡可拷贝——按值传只搬指针+元数据。
TEST_CASE("ImageFrame is trivially copyable", "[frame_pool][imageframe]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<ImageFrame>);
}

// (b) stride alignment — including non-4-divisible widths (grayscale padding).
// (b) stride 对齐——含非 4 整除宽度(模拟 grayscale padding)。
TEST_CASE("aligned_stride is 4-byte aligned and >= width*bpp", "[frame_pool][stride]") {
    const std::int32_t widths[] = {1, 2, 3, 7, 640, 641, 1920};
    const PixelFormat formats[] = {PixelFormat::RGBA8888, PixelFormat::RGB888, PixelFormat::Grayscale8};

    for (PixelFormat fmt : formats) {
        for (std::int32_t w : widths) {
            const std::int32_t s = aligned_stride(w, fmt);
            INFO("width=" << w << " bpp=" << bytes_per_pixel(fmt));
            REQUIRE(s % 4 == 0);
            REQUIRE(s >= w * bytes_per_pixel(fmt));
        }
    }

    // RGBA8888: stride == width*4 already 4-aligned, no padding.
    // RGBA8888:stride == width*4 本就 4 对齐,无 padding。
    REQUIRE(aligned_stride(1920, PixelFormat::RGBA8888) == 1920 * 4);
    // Grayscale8 width=641: 641 rounds up to 644. / 灰度宽 641 向上取整到 644。
    REQUIRE(aligned_stride(641, PixelFormat::Grayscale8) == 644);

    // Pool reports the same stride as the helper. / 池上报的 stride 与辅助函数一致。
    FramePool pool(641, 8, PixelFormat::Grayscale8, 2);
    REQUIRE(pool.stride() == 644);
    REQUIRE(pool.stride() % 4 == 0);
}

// (a) Reuse proves zero runtime allocation: acquire all 3 then release all,
//     repeated 1000 rounds; deduped data() pointers == exactly 3.
// (a) 复用证运行期零分配:每轮先连 acquire 满 3、再全 release,循环 1000 轮;
//     data() 指针去重后恰为 3(三槽全程复用、一个没漏)。
TEST_CASE("FramePool reuses a fixed set of slots (zero runtime allocation)", "[frame_pool][zero_alloc]") {
    constexpr std::size_t kSlots = 3;
    FramePool pool(64, 32, PixelFormat::RGBA8888, kSlots);

    std::set<std::byte*> seen;
    for (int round = 0; round < 1000; ++round) {
        std::array<ImageFrame, kSlots> held{};
        for (std::size_t i = 0; i < kSlots; ++i) {
            auto f = pool.acquire();
            REQUIRE(f.has_value());
            seen.insert(f->data);
            held[i] = *f;
        }
        for (const auto& f : held)
            pool.release(f);
    }

    REQUIRE(seen.size() == kSlots);
}

// (d) Write/read roundtrip, and a released slot is re-acquirable.
// (d) 读写回环,且 release 后该 slot 可再次 acquire。
TEST_CASE("FramePool slot is writable, read-back consistent, and re-acquirable", "[frame_pool][roundtrip]") {
    FramePool pool(16, 4, PixelFormat::RGBA8888, 2);

    auto f = pool.acquire();
    REQUIRE(f.has_value());

    // Fill the whole slot row-by-row with a known pattern. / 按行用已知图案写满整 slot。
    const std::int32_t row_bytes = f->width * bytes_per_pixel(f->format);
    for (std::int32_t y = 0; y < f->height; ++y) {
        std::byte* row = f->data + static_cast<std::size_t>(y) * f->stride;
        for (std::int32_t x = 0; x < row_bytes; ++x)
            row[x] = static_cast<std::byte>((y * 31 + x) & 0xFF);
    }
    // Read back and verify. / 读回校验。
    for (std::int32_t y = 0; y < f->height; ++y) {
        const std::byte* row = f->data + static_cast<std::size_t>(y) * f->stride;
        for (std::int32_t x = 0; x < row_bytes; ++x)
            REQUIRE(row[x] == static_cast<std::byte>((y * 31 + x) & 0xFF));
    }

    std::byte* freed = f->data;
    pool.release(*f);

    // After release the slot returns to the free pool and can be re-acquired.
    // release 后该 slot 回到空闲池,可被再次 acquire。
    auto g = pool.acquire();
    REQUIRE(g.has_value());
    REQUIRE(g->data == freed); // LIFO: same slot comes back / LIFO:同一槽回来
}

// (e) Metadata passthrough.
// (e) 元数据透传。
TEST_CASE("FramePool passes metadata through unchanged", "[frame_pool][metadata]") {
    FramePool pool(320, 240, PixelFormat::RGB888, 4);
    REQUIRE(pool.width() == 320);
    REQUIRE(pool.height() == 240);
    REQUIRE(pool.format() == PixelFormat::RGB888);

    auto f = pool.acquire(123456789);
    REQUIRE(f.has_value());
    REQUIRE(f->width == 320);
    REQUIRE(f->height == 240);
    REQUIRE(f->format == PixelFormat::RGB888);
    REQUIRE(f->stride == pool.stride());
    REQUIRE(f->timestamp_ns == 123456789);
}

// (f) Pool-full boundary: acquire returns nullopt without crash/alloc; releasing
//     one frees capacity again. data() stays inside storage bounds.
// (f) 池满边界:acquire 返回 nullopt 不崩不分配;release 一个后又有容量。
//     data() 不越界。
TEST_CASE("FramePool acquire fails gracefully when full", "[frame_pool][full]") {
    constexpr std::size_t kSlots = 3;
    FramePool pool(32, 16, PixelFormat::RGBA8888, kSlots);

    const std::size_t slot_bytes = static_cast<std::size_t>(pool.stride()) * pool.height();

    std::array<ImageFrame, kSlots> held{};
    for (std::size_t i = 0; i < kSlots; ++i) {
        auto f = pool.acquire();
        REQUIRE(f.has_value());
        held[i] = *f;
    }
    // Pool is LIFO, so acquisition order is not address order — derive the true
    // storage base as the minimum pointer, then bounds-check every slot.
    // 池是 LIFO,取出顺序非地址顺序——以最小指针为存储基址,再逐槽查越界。
    std::byte* base = held[0].data;
    for (const auto& f : held)
        if (f.data < base) base = f.data;
    for (const auto& f : held) {
        const std::ptrdiff_t off = f.data - base;
        REQUIRE(off >= 0);
        REQUIRE(static_cast<std::size_t>(off) % slot_bytes == 0);
        REQUIRE(static_cast<std::size_t>(off) < slot_bytes * kSlots);
    }
    REQUIRE(pool.in_use() == kSlots);
    REQUIRE(pool.available() == 0);

    // Full → nullopt, no crash, no allocation. / 满 → nullopt,不崩不分配。
    REQUIRE_FALSE(pool.acquire().has_value());

    // Release one → capacity available again. / release 一个 → 又有容量。
    pool.release(held[kSlots - 1]);
    REQUIRE(pool.available() == 1);
    REQUIRE(pool.acquire().has_value());
}
