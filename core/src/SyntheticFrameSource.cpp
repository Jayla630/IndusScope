#include "indusscope/core/SyntheticFrameSource.h"

#include <cassert>
#include <cstddef>

#include "indusscope/core/TestPattern.h"

namespace indusscope::core {

SyntheticFrameSource::SyntheticFrameSource(std::int32_t width, std::int32_t height, std::size_t slot_count)
    : m_pool(width, height, PixelFormat::RGBA8888, slot_count)
{
    // Gradient divide-by-zero guard moved to paintTestPattern (the shared drawing
    // entry point) so it covers any external slot too — see TestPattern.cpp.
    // FramePool already enforces width/height >= 1; nothing more to assert here.
    // 渐变除零守卫已挪进 paintTestPattern(共享绘制入口),连任意外部 slot 一并管住
    // ——见 TestPattern.cpp。FramePool 已挡 >=1,本层无需再断言。
}

std::optional<ImageFrame> SyntheticFrameSource::nextFrame(std::int64_t timestamp_ns) {
    std::optional<ImageFrame> frame = m_pool.acquire(timestamp_ns);
    if (!frame)            // pool full: no frame, counter not advanced / 池满:不出帧,计数不推进
        return std::nullopt;

    paintTestPattern(*frame, m_frame_index); // shared drawing, zero-copy into the slot / 共享绘制,零拷贝落进 slot
    ++m_frame_index;       // advance only after a frame is actually produced / 仅在真出帧后推进
    return frame;
}

void SyntheticFrameSource::recycle(const ImageFrame& frame) {
    m_pool.release(frame);
}

} // namespace indusscope::core
