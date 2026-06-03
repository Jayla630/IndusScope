#include "indusscope/core/MinMaxDownsampler.h"

#include <cstddef>
#include <vector>

namespace indusscope::core {

void minmax_downsample(const SamplePoint* data, std::size_t count,
                       std::size_t bucket_count,
                       std::vector<SamplePoint>& out)
{
    // Degenerate: count == 0 or bucket_count == 0 → clear out / 退化:count==0 或 bucket_count==0 → 清空
    if (count == 0 || bucket_count == 0) {
        out.clear();
        return;
    }

    // Passthrough: enough buckets to hold each point individually / 透传:桶数足够每点一桶
    if (count <= bucket_count) {
        out.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = data[i];
        }
        return;
    }

    // Pre-allocate worst-case output (2 points per bucket) / 预分配最坏情况输出(每桶两点)
    out.resize(2 * bucket_count);
    std::size_t out_idx = 0; // write cursor / 写游标

    for (std::size_t b = 0; b < bucket_count; ++b) {
        // Proportional bucketing, tail-inclusive / 比例分桶,尾点不丢
        // lo = b * count / bucket_count;  hi = (b+1) * count / bucket_count
        std::size_t lo = b * count / bucket_count;
        std::size_t hi = (b + 1) * count / bucket_count;

        // Empty bucket guard — kept for robustness / 空桶守卫——保留以防万一
        if (lo >= hi) {
            continue;
        }

        // Scan bucket to find min-value and max-value sample points / 扫描桶内找最小值点和最大值点
        double      min_val = data[lo].value;
        double      max_val = data[lo].value;
        std::size_t min_idx = lo;
        std::size_t max_idx = lo;

        for (std::size_t i = lo + 1; i < hi; ++i) {
            double v = data[i].value;
            if (v < min_val) {
                min_val = v;
                min_idx = i;
            }
            if (v > max_val) {
                max_val = v;
                max_idx = i;
            }
        }

        // Emit in source-index order — preserves signal envelope / 按源索引顺序发射——保留信号包络
        if (min_idx <= max_idx) {
            out[out_idx] = data[min_idx];
            ++out_idx;
            if (min_val != max_val) { // flat bucket → single point / 平桶 → 单点
                out[out_idx] = data[max_idx];
                ++out_idx;
            }
        } else {
            // max occurs earlier in source → emit it first / max 在源中更早出现 → 先发
            out[out_idx] = data[max_idx];
            ++out_idx;
            if (min_val != max_val) {
                out[out_idx] = data[min_idx];
                ++out_idx;
            }
        }
    }

    // Shrink to actual written count (steady-state zero alloc) / 缩至实际写入数(稳态零分配)
    out.resize(out_idx);
}

} // namespace indusscope::core
