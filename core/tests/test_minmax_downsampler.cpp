#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "indusscope/core/MinMaxDownsampler.h"
#include "indusscope/core/SamplePoint.h"

#include <vector>

using indusscope::core::minmax_downsample;
using indusscope::core::SamplePoint;

// ---------------------------------------------------------------------------
// 1. Passthrough — bucket_count >= count → point-for-point equality
//    透传——bucket_count >= count → 逐点等价
// ---------------------------------------------------------------------------

TEST_CASE("minmax_downsample passthrough when bucket_count >= count", "[core][minmax_downsampler]") {
    // Build 5 ascending points / 构造 5 个升序点
    std::vector<SamplePoint> data(5);
    for (int i = 0; i < 5; ++i) {
        data[i].timestamp_ns = 1000LL + static_cast<std::int64_t>(i) * 1000;
        data[i].value         = static_cast<double>(i);
    }

    std::vector<SamplePoint> out;

    SECTION("bucket_count > count") {
        minmax_downsample(data.data(), data.size(), 10, out);
        REQUIRE(out.size() == data.size());
        for (std::size_t i = 0; i < data.size(); ++i) {
            REQUIRE(out[i].timestamp_ns == data[i].timestamp_ns);
            REQUIRE(out[i].value         == data[i].value);
        }
    }

    SECTION("bucket_count == count") {
        minmax_downsample(data.data(), data.size(), data.size(), out);
        REQUIRE(out.size() == data.size());
        for (std::size_t i = 0; i < data.size(); ++i) {
            REQUIRE(out[i].timestamp_ns == data[i].timestamp_ns);
            REQUIRE(out[i].value         == data[i].value);
        }
    }
}

// ---------------------------------------------------------------------------
// 2. Spike survival — single extreme inject, must appear in output
//    尖峰存活——注入单个极值,必须在输出中出现
// ---------------------------------------------------------------------------

TEST_CASE("minmax_downsample spike survives downsampling", "[core][minmax_downsampler]") {
    // 100 points of ~0.0 background + one spike of 100.0 at index 50
    // 100 个 ~0.0 背景点 + index=50 处注入 100.0 尖峰
    std::vector<SamplePoint> data(100);
    for (int i = 0; i < 100; ++i) {
        data[i].timestamp_ns = 1000LL + static_cast<std::int64_t>(i) * 1000;
        data[i].value         = 0.0;
    }
    data[50].value = 100.0; // spike / 尖峰

    std::vector<SamplePoint> out;
    minmax_downsample(data.data(), data.size(), 10, out);

    // The spike must be present somewhere in the output / 尖峰必须出现在输出某处
    bool found = false;
    for (auto& p : out) {
        if (p.value == 100.0 && p.timestamp_ns == data[50].timestamp_ns) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

// ---------------------------------------------------------------------------
// 3. Monotonic — output timestamps are non-decreasing
//    单调性——输出 timestamp_ns 单调不减
// ---------------------------------------------------------------------------

TEST_CASE("minmax_downsample output timestamps non-decreasing", "[core][minmax_downsampler]") {
    // 1000 points with varying values / 1000 个值变化的点
    std::vector<SamplePoint> data(1000);
    for (int i = 0; i < 1000; ++i) {
        data[i].timestamp_ns = 1000LL + static_cast<std::int64_t>(i) * 1000;
        data[i].value         = static_cast<double>(i % 101); // sawtooth / 锯齿波
    }

    std::vector<SamplePoint> out;
    minmax_downsample(data.data(), data.size(), 50, out);

    REQUIRE(out.size() > 0);
    for (std::size_t i = 1; i < out.size(); ++i) {
        REQUIRE(out[i].timestamp_ns >= out[i - 1].timestamp_ns);
    }
}

// ---------------------------------------------------------------------------
// 4. Intra-bucket order — emission follows source-index order
//    桶内顺序——发射顺序跟随源索引
// ---------------------------------------------------------------------------

TEST_CASE("minmax_downsample intra-bucket order follows source index", "[core][minmax_downsampler]") {
    // Single bucket (bucket_count=1) so the entire array is one bucket.
    // 单桶 (bucket_count=1),整个数组就是一个桶。

    std::vector<SamplePoint> out;

    SECTION("min before max in source → min emitted first / min 在 max 之前 → min 先发") {
        // values: [10, 5, 3, 7, 20] — min @ idx 2 (3.0), max @ idx 4 (20.0)
        std::vector<SamplePoint> data(5);
        data[0] = {1000, 10.0};
        data[1] = {2000, 5.0};
        data[2] = {3000, 3.0};  // min
        data[3] = {4000, 7.0};
        data[4] = {5000, 20.0}; // max

        minmax_downsample(data.data(), 5, 1, out);
        REQUIRE(out.size() == 2);
        REQUIRE(out[0].value == 3.0);   // min first (source-index smaller) / min 先发(源索引较小)
        REQUIRE(out[1].value == 20.0);  // max second / max 后发
    }

    SECTION("max before min in source → max emitted first / max 在 min 之前 → max 先发") {
        // values: [20, 7, 3, 5, 10] — max @ idx 0 (20.0), min @ idx 2 (3.0)
        std::vector<SamplePoint> data(5);
        data[0] = {1000, 20.0}; // max
        data[1] = {2000, 7.0};
        data[2] = {3000, 3.0};  // min
        data[3] = {4000, 5.0};
        data[4] = {5000, 10.0};

        minmax_downsample(data.data(), 5, 1, out);
        REQUIRE(out.size() == 2);
        REQUIRE(out[0].value == 20.0);  // max first (source-index smaller) / max 先发(源索引较小)
        REQUIRE(out[1].value == 3.0);   // min second / min 后发
    }

    SECTION("flat bucket (all same value) → single point / 平桶(全同值) → 单点") {
        // values: [5.0, 5.0, 5.0, 5.0, 5.0] — all flat
        std::vector<SamplePoint> data(5);
        for (int i = 0; i < 5; ++i) {
            data[i] = {1000LL + i * 1000, 5.0};
        }

        minmax_downsample(data.data(), 5, 1, out);
        REQUIRE(out.size() == 1);
        REQUIRE(out[0].value == 5.0);
    }
}

// ---------------------------------------------------------------------------
// 5. Degenerate safety — count==0, count==1, bucket_count==0 all safe
//    退化安全——count==0、count==1、bucket_count==0 均不崩
// ---------------------------------------------------------------------------

TEST_CASE("minmax_downsample degenerate inputs do not crash", "[core][minmax_downsampler]") {
    std::vector<SamplePoint> out;

    SECTION("count == 0 → out empty / count==0 → out 为空") {
        minmax_downsample(nullptr, 0, 10, out);
        REQUIRE(out.empty());
    }

    SECTION("bucket_count == 0 → out empty / bucket_count==0 → out 为空") {
        SamplePoint dummy{1000, 1.0};
        minmax_downsample(&dummy, 1, 0, out);
        REQUIRE(out.empty());
    }

    SECTION("count == 1 → passthrough / count==1 → 透传") {
        SamplePoint dummy{1000, 42.0};
        minmax_downsample(&dummy, 1, 10, out);
        REQUIRE(out.size() == 1);
        REQUIRE(out[0].timestamp_ns == 1000);
        REQUIRE(out[0].value         == 42.0);
    }

    SECTION("count == 1, bucket_count == 1 → passthrough / count==1, bucket_count==1 → 透传") {
        SamplePoint dummy{2000, -3.14};
        minmax_downsample(&dummy, 1, 1, out);
        REQUIRE(out.size() == 1);
        REQUIRE(out[0].timestamp_ns == 2000);
        REQUIRE(out[0].value         == -3.14);
    }
}

// ---------------------------------------------------------------------------
// 6. Upper bound — output size ≤ 2 × bucket_count
//    输出上界——out.size() ≤ 2 × bucket_count
// ---------------------------------------------------------------------------

TEST_CASE("minmax_downsample output size bounded by 2 * bucket_count", "[core][minmax_downsampler]") {
    // Varying values to exercise both min and max per bucket / 变化值使每桶都有不同的 min 和 max
    std::vector<SamplePoint> data(1000);
    for (int i = 0; i < 1000; ++i) {
        data[i].timestamp_ns = 1000LL + static_cast<std::int64_t>(i) * 1000;
        data[i].value         = static_cast<double>(i); // strictly increasing / 严格递增
    }

    std::vector<SamplePoint> out;

    // Test a few representative bucket counts / 测试几个代表性桶数
    const std::size_t test_buckets[] = {1, 5, 20, 100, 500};
    for (std::size_t bc : test_buckets) {
        minmax_downsample(data.data(), data.size(), bc, out);
        REQUIRE(out.size() <= 2 * bc);
        REQUIRE(out.size() > 0);
    }
}

// ---------------------------------------------------------------------------
// 7. Buffer reuse — same out vector across calls, second call shrinks
//    缓冲复用——同一 out vector 跨调用,第二次输出更少,结果正确无残留
// ---------------------------------------------------------------------------

TEST_CASE("minmax_downsample buffer reuse with shrinking output", "[core][minmax_downsampler]") {
    std::vector<SamplePoint> out;

    // First call: 500-point sawtooth → 25 buckets → ~50 output points
    // 第一次:500 点锯齿波 → 25 桶 → ~50 个输出点
    {
        std::vector<SamplePoint> data(500);
        for (int i = 0; i < 500; ++i) {
            data[i].timestamp_ns = static_cast<std::int64_t>(i) * 1000;
            data[i].value         = static_cast<double>(i % 50); // sawtooth / 锯齿波
        }
        minmax_downsample(data.data(), data.size(), 25, out);
        std::size_t first_size = out.size();
        REQUIRE(first_size > 0);
        REQUIRE(first_size <= 50);
    }

    // Second call: 5-point single-bucket → at most 2 output points → fewer than first
    // 第二次:5 点单桶 → 最多 2 点 → 少于第一次
    {
        std::vector<SamplePoint> data(5);
        data[0] = {1000, 0.0};
        data[1] = {2000, -1.0}; // min @ idx 1 / min 在 idx 1
        data[2] = {3000, 0.5};
        data[3] = {4000, 2.0};  // max @ idx 3 / max 在 idx 3
        data[4] = {5000, 1.0};

        minmax_downsample(data.data(), data.size(), 1, out);
        REQUIRE(out.size() == 2);
        REQUIRE(out[0].value == -1.0); // min first (idx 1 < idx 3)
        REQUIRE(out[1].value ==  2.0); // max second
    }

    // Third call: even smaller — flat values → single point
    // 第三次:更少——平值 → 单点
    {
        std::vector<SamplePoint> data(3);
        data[0] = {100, 7.0};
        data[1] = {200, 7.0};
        data[2] = {300, 7.0};

        minmax_downsample(data.data(), data.size(), 2, out);
        // count=3 > bucket_count=2, but with flat values → few output points
        REQUIRE(out.size() <= 4); // upper bound: 2 * 2 = 4
        REQUIRE(out.size() >= 1);
        // Verify each output point has value 7.0 / 验证每个输出点都是 7.0
        for (auto& p : out) {
            REQUIRE(p.value == 7.0);
        }
    }
}

// ---------------------------------------------------------------------------
// 8. Non-divisible tail — extreme in last partial bucket survives
//    非整除尾点——最后不满桶的极值必须存活
// ---------------------------------------------------------------------------

TEST_CASE("minmax_downsample non-divisible tail partial bucket extreme survives", "[core][minmax_downsampler]") {
    // count=2001, bucket_count=1000: last bucket covers indices ~1998–2000
    // count=2001, bucket_count=1000:尾桶覆盖索引约 1998–2000
    // Place extreme at the very last point (index 2000), assert it survives.
    // 将极值放在最后一点 (index 2000),断言它在输出里。

    std::vector<SamplePoint> data(2001);
    for (int i = 0; i < 2001; ++i) {
        data[i].timestamp_ns = 1000LL + static_cast<std::int64_t>(i) * 1000;
        data[i].value         = 0.0;
    }
    data[2000].value = 999.0; // extreme at tail / 尾点极值

    std::vector<SamplePoint> out;
    minmax_downsample(data.data(), data.size(), 1000, out);

    // Upper bound / 上界检查
    REQUIRE(out.size() <= 2000);

    // The extreme value must appear / 极值必须出现
    bool found_extreme = false;
    for (auto& p : out) {
        if (p.value == 999.0 && p.timestamp_ns == data[2000].timestamp_ns) {
            found_extreme = true;
            break;
        }
    }
    REQUIRE(found_extreme);

    // Additionally verify: the last data point's timestamp IS in output
    // 额外验证:最后数据点的时间戳在输出中有对应
    // (the extreme IS the last point, so this is already covered above)
    // (极值即最后一点,上面已覆盖)

    // Also verify that earlier points made it in / 也验证更早的点有输出
    REQUIRE(out.size() > 0);
    // Output must be monotonically non-decreasing / 输出必须单调不减
    for (std::size_t i = 1; i < out.size(); ++i) {
        REQUIRE(out[i].timestamp_ns >= out[i - 1].timestamp_ns);
    }
}
