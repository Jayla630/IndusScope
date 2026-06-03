#pragma once

/// @file MinMaxDownsampler.h
/// Min/Max envelope downsampling free function — extracts the visual envelope
/// of a time-series for bandwidth-limited curve rendering.
/// Min/Max 包络降采样自由函数——提取时间序列的视觉包络,用于带宽受限的曲线渲染。

#include "indusscope/core/SamplePoint.h"

#include <cstddef>
#include <vector>

namespace indusscope::core {

/// Min/Max envelope downsampling for curve rendering on finite pixel grids.
/// Min/Max 包络降采样,用于有限像素网格上的曲线渲染。
///
/// Accepts an ordered array of SamplePoint (non-decreasing timestamp_ns)
/// and reduces it to at most 2 × @p bucket_count envelope points by
/// recording the min-value and max-value sample in each index-proportional
/// bucket and emitting them in source-index order (preserves the true
/// signal envelope without introducing visual zigzags).
/// 输入按 timestamp_ns 非降序排列的 SamplePoint 数组,将其降采样到最多
/// 2 × @p bucket_count 个包络点:在每个按索引比例划分的桶内记录最小值点
/// 与最大值点,按源索引顺序发射(保留信号真实包络,不引入视觉锯齿)。
///
/// ## Bucketing strategy — proportional, tail-inclusive / 分桶策略——比例切分,尾点不丢
///
/// Bucket boundaries are computed as:
/// 桶边界计算:
///     lo = b × count / bucket_count;
///     hi = (b+1) × count / bucket_count;
///
/// This is *index-proportional*, not fixed points-per-bucket.  Because the
/// input is uniformly sampled (fixed-period grid), index ≈ X and this is
/// both simple and correct.  The final bucket covers up to index count-1
/// inclusively — no tail sample is dropped.
/// 按索引比例而非固定每桶点数。由于输入为均匀采样(固定周期网格),index ≈ X,
/// 此法最简且正确。最后一个桶覆盖到 index count-1(含),尾点不丢。
///
/// @note For non-uniformly-sampled data, the caller should bucket by
///       timestamp_ns instead of index; this function explicitly does NOT
///       handle that case. / 非均匀采样数据应由调用方按 timestamp_ns 分桶;
///       本函数明确不处理该情况。
///
/// ## Per-bucket rules / 桶内规则
///
/// 1. Scan all points in the bucket to find the min-value sample and
///    max-value sample, tracking both the extreme value and its source index.
///    扫描桶内所有点,找到最小值点和最大值点,同时记录极值及源索引。
/// 2. Emit the two points in source-index order (smaller index first).
///    按源索引顺序发射两点(索引小的先发)。
/// 3. When min and max are the same sample (flat bucket or single-point
///    bucket), emit only one point. / min 与 max 为同一点时(平桶或单点桶)只发一点。
///
/// ## Algorithm choice — why Min/Max, not LTTB or M4 / 算法选型——为何 Min/Max 而非 LTTB 或 M4
///
/// - **Why not LTTB** (Largest-Triangle Three Buckets): LTTB selects one
///   representative point per bucket to minimise visual error in the
///   *average* sense.  It can and will smooth out single-sample spikes —
///   exactly the kind of glitch an oscilloscope-style viewer must preserve.
///   Min/Max guarantees the envelope survives at any bucket granularity.
///   **为何不用 LTTB**:LTTB 每桶选一个代表点,在*平均*意义上最小化视觉误差。
///   它会(也必然会)平滑掉单采样点尖峰——正是示波器风格查看器必须保留的毛刺。
///   Min/Max 在任何桶粒度下保证包络存活。
/// - **Why not M4** (min/max/median/mean — 4 points per bucket): At typical
///   screen widths (~2000 px), 2 points per bucket already yield ≤ 4000
///   rendered points.  The inter-sample gap at that density is well below
///   one pixel; adding two more points per bucket is pure complexity with
///   no visual benefit.  M4 may be revisited when targeting ultra-wide
///   (>4000 px) or print-resolution rendering. / **为何暂不上 M4**:
///   典型屏宽 ~2000px 下 2 点/桶已产生 ≤4000 个渲染点,此密度下采样点间距
///   远小于单像素;每桶多加两点纯属徒增复杂度而无视觉收益。若将来适配超宽屏
///   (>4000px) 或打印分辨率可重新评估 M4。
///
/// ## Memory contract / 内存契约
///
/// - @p out is caller-owned and reusable across invocations.
///   @p out 由调用方持有,可跨多次调用复用。
/// - Internally `out.resize(2 × bucket_count)` once to pre-allocate the
///   worst-case output, then fills by index assignment (no push_back).
///   After filling, `out.resize(actual_count)` shrinks to exact size.
///   内部先 `out.resize(2 × bucket_count)` 一次性预分配最坏情况输出,
///   再按索引赋值(无 push_back)。填完后 `out.resize(实际数量)` 缩至精确大小。
/// - Steady state: zero heap allocation when the caller's vector capacity
///   is already sufficient. / 稳态:调用方 vector 容量足够时零堆分配。
///
/// @param data        Pointer to sorted SamplePoint array (non-decreasing timestamp_ns).
///                    指向按 timestamp_ns 非降序排列的 SamplePoint 数组。
/// @param count       Number of elements in @p data. / @p data 中的元素个数。
/// @param bucket_count Target number of buckets (typically screen width in pixels).
///                    目标桶数(通常是屏幕像素宽度)。
/// @param out         [out] Result vector — resized to fit the output envelope points.
///                    [出参] 结果 vector——resize 至输出的包络点数。
void minmax_downsample(const SamplePoint* data, std::size_t count,
                       std::size_t bucket_count,
                       std::vector<SamplePoint>& out);

} // namespace indusscope::core
