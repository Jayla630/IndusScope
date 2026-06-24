#include "indusscope/core/FramePool.h"

#include <algorithm>
#include <cassert>

namespace indusscope::core {

FramePool::FramePool(std::int32_t width, std::int32_t height, PixelFormat format, std::size_t slot_count)
    : m_width(width)
    , m_height(height)
    , m_stride(aligned_stride(width, format))
    , m_format(format)
    , m_slot_count(slot_count)
    // slot_bytes / total computed in size_t to avoid int32 overflow on huge frames
    // slot_bytes / 总量用 size_t 计算,防超大帧 int32 溢出
    , m_slot_bytes(static_cast<std::size_t>(m_stride) * static_cast<std::size_t>(height))
    , m_free_top(slot_count)
{
    // Required 1: reject 0 dimensions — height==0 ⇒ m_slot_bytes==0 ⇒ divide-by-zero in release().
    // 必修 1:挡 0 维度——height==0 ⇒ m_slot_bytes==0 ⇒ release() 里整数除零。
    // Contract violation = programmer error: assert, don't throw (core ships to ARM, save exceptions).
    // 契约违反属程序员误用:用 assert、不抛异常(core 要上 ARM,异常能省则省)。
    assert(width >= 1 && height >= 1 && slot_count >= 1 && "FramePool: width/height/slot_count must be >= 1");

    // One-shot allocations at construction — the only allocations in this class.
    // 构造期一次性分配——本类唯一的分配点。
    m_storage.resize(m_slot_bytes * slot_count);

    // Free stack: fixed-size array pre-filled with all slot indices. acquire/release
    // only move the top index, never push_back/pop_back → never reallocate.
    // 空闲栈:定长数组,预填全部 slot 索引。acquire/release 只挪栈顶索引,
    // 不 push_back/pop_back → 绝不重分配。
    m_free.resize(slot_count);
    for (std::size_t i = 0; i < slot_count; ++i)
        m_free[i] = i;
}

std::optional<ImageFrame> FramePool::acquire(std::int64_t timestamp_ns) {
    if (m_free_top == 0)  // pool full / 池满
        return std::nullopt;

    const std::size_t idx = m_free[--m_free_top]; // pop: just move the top index / 出栈:只挪栈顶,零分配

    ImageFrame frame;
    frame.data         = m_storage.data() + idx * m_slot_bytes;
    frame.timestamp_ns = timestamp_ns;
    frame.width        = m_width;
    frame.height       = m_height;
    frame.stride       = m_stride;
    frame.format       = m_format;
    // NOTE: slot contents intentionally left undefined — caller must overwrite. / 注意:slot 内容刻意不清零——使用方须整帧写满。
    return frame;
}

void FramePool::release(const ImageFrame& frame) {
    const std::byte* base = m_storage.data();
    const std::ptrdiff_t offset = frame.data - base;

    // Required 2①: guard wild pointers / out-of-range — offset non-negative, slot-aligned, idx in range.
    // 必修 2①:挡野指针/越界——offset 非负、能被 slot 整除、idx 在范围内。
    assert(offset >= 0 && "FramePool::release: frame.data below pool storage");
    assert(static_cast<std::size_t>(offset) % m_slot_bytes == 0 && "FramePool::release: frame.data not slot-aligned");
    const std::size_t idx = static_cast<std::size_t>(offset) / m_slot_bytes;
    assert(idx < m_slot_count && "FramePool::release: frame.data above pool storage");

    // Required 2②: guard double-release — pushing the same slot twice would let two
    // users hold one slot (S2.6b: cross-thread tearing) and overflow the free stack.
    // 必修 2②:挡重复 release——同一 slot 入栈两次会让两个使用者持有同一槽
    // (S2.6b 跨线程画面撕裂),并撑爆空闲栈。
    assert(std::find(m_free.begin(), m_free.begin() + m_free_top, idx) == m_free.begin() + m_free_top &&
           "FramePool::release: slot already free (double release)");

    m_free[m_free_top++] = idx; // push: write into reserved slot, move top → zero allocation / 入栈:写入已分配槽位,挪栈顶 → 零分配
}

} // namespace indusscope::core
