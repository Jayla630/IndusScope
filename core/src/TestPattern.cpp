#include "indusscope/core/TestPattern.h"

#include <cassert>
#include <cstddef>

namespace indusscope::core {

void paintTestPattern(const ImageFrame& frame, std::uint64_t frame_index) noexcept {
    // Divide-by-zero guard follows the drawing to its new entry point: the gradient
    // denominators are (width-1)/(height-1). Moved here from SyntheticFrameSource's
    // ctor so any external slot (incl. a 1px buffer) that calls this is guarded too.
    // 除零守卫跟到新入口:渐变分母 (width-1)/(height-1)。从 SyntheticFrameSource
    // 构造函数挪来——任意外部 slot(含 1px buffer)调它也受保护。
    assert(frame.width >= 2 && frame.height >= 2 &&
           "paintTestPattern: width/height must be >= 2 for the gradient");
    assert(frame.format == PixelFormat::RGBA8888 &&
           "paintTestPattern: only RGBA8888 supported");

    const std::int32_t width  = frame.width;
    const std::int32_t height = frame.height;
    const std::int32_t stride = frame.stride;

    // Bar column for this frame. Land it in int up front to dodge -Wsign-compare
    // against int x below. / 本帧竖条所在列。先落成 int,免得下面与 int x 比较触发 -Wsign-compare。
    const int bar_col = static_cast<int>(frame_index % static_cast<std::uint64_t>(width));

    // Paint the full valid region: row = data + y*stride (NOT y*width*bpp),
    // column step = 4 B (RGBA8888). No alloc, no push_back in the loop body.
    // 画满有效区:row = data + y*stride(非 y*width*bpp),列步进 4 B(RGBA8888)。
    // 循环体内零分配、无 push_back。
    for (std::int32_t y = 0; y < height; ++y) {
        std::byte* row = frame.data + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        const auto g = static_cast<std::byte>(y * 255 / (height - 1)); // green ramp down rows / 绿沿行渐变
        for (std::int32_t x = 0; x < width; ++x) {
            std::byte* px = row + static_cast<std::size_t>(x) * 4u;
            if (x == bar_col) {
                // moving white bar overrides background / 移动白竖条盖掉背景
                px[0] = static_cast<std::byte>(255);
                px[1] = static_cast<std::byte>(255);
                px[2] = static_cast<std::byte>(255);
                px[3] = static_cast<std::byte>(255);
            } else {
                px[0] = static_cast<std::byte>(x * 255 / (width - 1)); // red ramp across cols / 红沿列渐变
                px[1] = g;
                px[2] = static_cast<std::byte>(0);
                px[3] = static_cast<std::byte>(255);
            }
        }
        // RGBA8888: stride == width*4, no padding — nothing to skip. / 无 padding,不写不读。
    }
}

} // namespace indusscope::core
