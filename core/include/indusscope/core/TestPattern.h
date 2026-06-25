#pragma once

#include <cstdint>

#include "indusscope/core/ImageFrame.h"

namespace indusscope::core {

/// Width of the moving white bar, in pixels. Named, not a magic number.
/// 移动白竖条宽(像素)。具名非魔数。
inline constexpr int kBarWidth = 1;

/// Paint the deterministic moving test pattern into @p frame (RGBA8888 only).
/// 把确定性移动测试图案画进 @p frame(仅 RGBA8888)。
///
/// Pattern = pure function of (frame_index, x, y):
/// 图案 = (frame_index, x, y) 的纯函数:
/// - background: gradient R=x·255/(w-1), G=y·255/(h-1), B=0, A=255.
///   背景:渐变 R=x·255/(w-1),G=y·255/(h-1),B=0,A=255。
/// - moving bar: white column at col = frame_index % width.
///   移动竖条:列 = frame_index % width 处的白竖条。
///
/// Addresses every row via row = data + y*stride (NOT y*width*bpp). Zero alloc.
/// 逐行寻址 row = data + y*stride(非 y*width*bpp)。零分配。
///
/// Extracted from SyntheticFrameSource so a worker can draw straight into any
/// external slot (e.g. a LatestFrameBuffer write slot) for end-to-end zero-copy.
/// 从 SyntheticFrameSource 抽出,供 worker 直接画进任意外部 slot
/// (如 LatestFrameBuffer 的 write slot)实现端到端零拷贝。
void paintTestPattern(const ImageFrame& frame, std::uint64_t frame_index) noexcept;

} // namespace indusscope::core
