#include "indusscope/core/SyntheticFrameSource.h"

#include <cassert>
#include <cstddef>

namespace indusscope::core {

SyntheticFrameSource::SyntheticFrameSource(std::int32_t width, std::int32_t height, std::size_t slot_count)
    : m_pool(width, height, PixelFormat::RGBA8888, slot_count)
{
    // Gradient denominator is (width-1)/(height-1); width/height==1 ⇒ divide-by-zero.
    // FramePool guards >=1, which doesn't cover this layer — assert >=2 here.
    // 渐变分母是 (width-1)/(height-1);width/height==1 ⇒ 除零。
    // FramePool 只管 >=1,管不到这层——本层断言 >=2。
    assert(width >= 2 && height >= 2 && "SyntheticFrameSource: width/height must be >= 2 for the gradient");
}

std::optional<ImageFrame> SyntheticFrameSource::nextFrame(std::int64_t timestamp_ns) {
    std::optional<ImageFrame> frame = m_pool.acquire(timestamp_ns);
    if (!frame)            // pool full: no frame, counter not advanced / 池满:不出帧,计数不推进
        return std::nullopt;

    const std::int32_t width  = frame->width;
    const std::int32_t height = frame->height;
    const std::int32_t stride = frame->stride;

    // Bar column for this frame. Land it in int up front to dodge -Wsign-compare
    // against int x below. / 本帧竖条所在列。先落成 int,免得下面与 int x 比较触发 -Wsign-compare。
    const int bar_col = static_cast<int>(m_frame_index % static_cast<std::uint64_t>(width));

    // Paint the full valid region: row = data + y*stride (NOT y*width*bpp),
    // column step = 4 B (RGBA8888). No alloc, no push_back in the loop body.
    // 画满有效区:row = data + y*stride(非 y*width*bpp),列步进 4 B(RGBA8888)。
    // 循环体内零分配、无 push_back。
    for (std::int32_t y = 0; y < height; ++y) {
        std::byte* row = frame->data + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
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

    ++m_frame_index; // advance only after a frame is actually produced / 仅在真出帧后推进
    return frame;
}

void SyntheticFrameSource::recycle(const ImageFrame& frame) {
    m_pool.release(frame);
}

} // namespace indusscope::core
