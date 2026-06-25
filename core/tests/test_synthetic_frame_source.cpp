#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "indusscope/core/ImageFrame.h"
#include "indusscope/core/SyntheticFrameSource.h"

using namespace indusscope::core;

namespace {

// Test-side replica of the source's pure pattern function. Any drift between
// this and the implementation is exactly what the assertions catch.
// 测试侧复刻源的纯图案函数。本函数与实现一旦不一致,正是断言要抓的。
std::array<std::uint8_t, 4> expected_pixel(std::uint64_t frame_index, std::int32_t x, std::int32_t y,
                                           std::int32_t width, std::int32_t height) {
    const int bar_col = static_cast<int>(frame_index % static_cast<std::uint64_t>(width));
    if (x == bar_col)
        return {255, 255, 255, 255};
    return {static_cast<std::uint8_t>(x * 255 / (width - 1)),
            static_cast<std::uint8_t>(y * 255 / (height - 1)),
            0, 255};
}

// Read one RGBA pixel from a frame using stride addressing.
// 用 stride 寻址从帧读一个 RGBA 像素。
std::array<std::uint8_t, 4> read_pixel(const ImageFrame& f, std::int32_t x, std::int32_t y) {
    const std::byte* px = f.data + static_cast<std::size_t>(y) * static_cast<std::size_t>(f.stride)
                          + static_cast<std::size_t>(x) * 4u;
    return {static_cast<std::uint8_t>(px[0]), static_cast<std::uint8_t>(px[1]),
            static_cast<std::uint8_t>(px[2]), static_cast<std::uint8_t>(px[3])};
}

} // namespace

// (a) Determinism — sampled points equal the pure-function value.
// (a) 确定性——抽样点等于纯函数值。
TEST_CASE("SyntheticFrameSource: sampled pixels match the pattern function", "[synthetic_source][pattern]") {
    constexpr std::int32_t W = 8, H = 6;
    SyntheticFrameSource src(W, H, 3);

    // Advance to frame_index = K = 3 (recycle between so the pool never fills).
    // 推进到 frame_index = K = 3(中间 recycle,池不占满)。
    constexpr std::uint64_t K = 3;
    for (std::uint64_t i = 0; i < K; ++i) {
        auto warm = src.nextFrame(0);
        REQUIRE(warm);
        src.recycle(*warm);
    }
    REQUIRE(src.frameIndex() == K);

    auto frame = src.nextFrame(0);
    REQUIRE(frame);

    // Sample: four corners, center, the bar column, and each row's last valid pixel.
    // 抽样:四角、中心、竖条列、每行末尾有效像素。
    const std::array<std::pair<std::int32_t, std::int32_t>, 5> pts = {{
        {0, 0}, {W - 1, 0}, {0, H - 1}, {W - 1, H - 1}, {W / 2, H / 2},
    }};
    for (auto [x, y] : pts) {
        INFO("sample (" << x << "," << y << ")");
        REQUIRE(read_pixel(*frame, x, y) == expected_pixel(K, x, y, W, H));
    }
    // Bar column for K=3 / K=3 的竖条列。
    REQUIRE(read_pixel(*frame, static_cast<std::int32_t>(K % W), H / 2)
            == (std::array<std::uint8_t, 4>{255, 255, 255, 255}));
    // Each row's last valid pixel / 每行末尾有效像素。
    for (std::int32_t y = 0; y < H; ++y) {
        INFO("row end y=" << y);
        REQUIRE(read_pixel(*frame, W - 1, y) == expected_pixel(K, W - 1, y, W, H));
    }
    src.recycle(*frame);
}

// (a') Reuse coverage — actively dirty a reused slot, then assert the WHOLE
// frame equals the pattern. A missed pixel keeps the 0xAB sentinel and fails.
// (a') 复用覆盖——主动把复用槽写脏,再断【整帧】等于图案。漏写的像素留 0xAB 哨兵被抓出。
TEST_CASE("SyntheticFrameSource: reused slot is fully overwritten (no stale bytes)", "[synthetic_source][overwrite]") {
    constexpr std::int32_t W = 8, H = 6;
    SyntheticFrameSource src(W, H, /*slot_count=*/1); // single slot forces reuse / 单槽强制复用

    // index 0 → dirty the whole valid region with a sentinel → recycle.
    // 出 index 0 → 把整个有效区写成哨兵 → 归还。
    auto f0 = src.nextFrame(0);
    REQUIRE(f0);
    for (std::int32_t y = 0; y < H; ++y) {
        std::byte* row = f0->data + static_cast<std::size_t>(y) * static_cast<std::size_t>(f0->stride);
        std::memset(row, 0xAB, static_cast<std::size_t>(W) * 4u);
    }
    src.recycle(*f0);

    // index 1 reuses the same slot — must repaint every pixel.
    // index 1 复用同槽——必须重画每个像素。
    auto f1 = src.nextFrame(0);
    REQUIRE(f1);
    for (std::int32_t y = 0; y < H; ++y) {
        for (std::int32_t x = 0; x < W; ++x) {
            INFO("pixel (" << x << "," << y << ")");
            REQUIRE(read_pixel(*f1, x, y) == expected_pixel(1, x, y, W, H));
        }
    }
    src.recycle(*f1);
}

// (b) Frame-by-frame advance — bar column tracks frame_index % width.
// (b) 逐帧推进——竖条列随 frame_index % width 平移。
TEST_CASE("SyntheticFrameSource: bar column advances with frame index", "[synthetic_source][advance]") {
    constexpr std::int32_t W = 8, H = 6;
    SyntheticFrameSource src(W, H, 3);

    for (std::uint64_t i = 0; i < 2 * W; ++i) { // wrap around once / 跨过一轮回绕
        REQUIRE(src.frameIndex() == i);
        auto frame = src.nextFrame(0);
        REQUIRE(frame);
        const std::int32_t expected_col = static_cast<std::int32_t>(i % W);
        // The white bar sits at expected_col on every row. / 每行白竖条都在 expected_col。
        REQUIRE(read_pixel(*frame, expected_col, 0)
                == (std::array<std::uint8_t, 4>{255, 255, 255, 255}));
        REQUIRE(read_pixel(*frame, expected_col, H - 1)
                == (std::array<std::uint8_t, 4>{255, 255, 255, 255}));
        REQUIRE(src.frameIndex() == i + 1); // counter advanced after producing / 出帧后计数推进
        src.recycle(*frame);
    }
}

// (c) Determinism / no clock read — two independent sources at the same index
// are byte-identical over the valid region (padding excluded).
// (c) 确定性/不读时钟——两个独立源在同一帧号下,有效区逐字节一致(不含 padding)。
TEST_CASE("SyntheticFrameSource: same frame index is byte-identical across instances", "[synthetic_source][deterministic]") {
    constexpr std::int32_t W = 8, H = 6;
    constexpr std::uint64_t K = 5;

    auto advance_to = [](SyntheticFrameSource& s, std::uint64_t k) {
        for (std::uint64_t i = 0; i < k; ++i) {
            auto warm = s.nextFrame(0);
            REQUIRE(warm);
            s.recycle(*warm);
        }
    };

    SyntheticFrameSource a(W, H, 3);
    SyntheticFrameSource b(W, H, 3);
    advance_to(a, K);
    advance_to(b, K);

    // Different injected timestamps must NOT change pixels (timestamp is metadata only).
    // 注入不同时间戳绝不能改变像素(时间戳只是元数据)。
    auto fa = a.nextFrame(111);
    auto fb = b.nextFrame(999);
    REQUIRE(fa);
    REQUIRE(fb);

    for (std::int32_t y = 0; y < H; ++y) {
        const std::byte* ra = fa->data + static_cast<std::size_t>(y) * static_cast<std::size_t>(fa->stride);
        const std::byte* rb = fb->data + static_cast<std::size_t>(y) * static_cast<std::size_t>(fb->stride);
        INFO("row y=" << y);
        REQUIRE(std::memcmp(ra, rb, static_cast<std::size_t>(W) * 4u) == 0); // valid region only / 仅有效区
    }
    a.recycle(*fa);
    b.recycle(*fb);
}

// (d) Timestamp passthrough — injected timestamp lands on the frame verbatim.
// (d) 时间戳透传——注入的时间戳原样落到帧上。
TEST_CASE("SyntheticFrameSource: timestamp is passed through to the frame", "[synthetic_source][timestamp]") {
    SyntheticFrameSource src(8, 6, 3);
    constexpr std::int64_t T = 1'234'567'890;
    auto frame = src.nextFrame(T);
    REQUIRE(frame);
    REQUIRE(frame->timestamp_ns == T);
    src.recycle(*frame);
}

// (e) Pool-full boundary — past slot_count, nextFrame returns nullopt without
// crashing or advancing; recycling one frees capacity again.
// (e) 池满边界——超过 slot_count,nextFrame 返回 nullopt 不崩不推进;归还一个又能出帧。
TEST_CASE("SyntheticFrameSource: returns nullopt when pool is full", "[synthetic_source][full]") {
    constexpr std::size_t N = 3;
    SyntheticFrameSource src(8, 6, N);

    // Hold N frames without recycling — fixed-size array, no push_back. / 攥住 N 帧不归还——定长数组,无 push_back。
    std::array<std::optional<ImageFrame>, N> held;
    for (std::size_t i = 0; i < N; ++i) {
        held[i] = src.nextFrame(0);
        REQUIRE(held[i]);
    }
    REQUIRE(src.frameIndex() == N);

    // Pool full → nullopt, no crash, counter NOT advanced. / 池满 → nullopt,不崩,计数不推进。
    auto overflow = src.nextFrame(0);
    REQUIRE_FALSE(overflow);
    REQUIRE(src.frameIndex() == N);

    // Recycle one → capacity restored → can produce again. / 归还一个 → 恢复容量 → 又能出帧。
    src.recycle(*held[N - 1]);
    held[N - 1].reset();
    auto again = src.nextFrame(0);
    REQUIRE(again);
    REQUIRE(src.frameIndex() == N + 1);

    src.recycle(*again);
    for (std::size_t i = 0; i < N - 1; ++i)
        src.recycle(*held[i]);
}
