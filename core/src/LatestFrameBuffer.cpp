#include "indusscope/core/LatestFrameBuffer.h"

#include <cassert>

namespace indusscope::core {

LatestFrameBuffer::LatestFrameBuffer(std::int32_t width, std::int32_t height, PixelFormat format)
    : m_pool(width, height, format, 3)
    , m_write_index(0)
    , m_read_index(1)
    , m_back(2) // initial roles {write=0, read=1, back=2}, no dirty / 初始角色 {write=0,read=1,back=2},无 dirty
{
    // One-shot: take all 3 pool slots and hold their fixed views for life. Steady
    // state never touches the pool free-list again — only the atomic index is juggled.
    // 一次性:取满 3 槽,终生持有其固定视图。稳态绝不再碰池的 free-list——只倒原子索引。
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        std::optional<ImageFrame> slot = m_pool.acquire(0);
        assert(slot && "LatestFrameBuffer: pool must yield 3 slots at construction");
        m_slots[i] = *slot;
    }
    // No dtor releases needed: FramePool's storage is freed by its own dtor, and it
    // holds no "all slots returned" assertion. / 无需 dtor 逐个 release:FramePool 存储
    // 随其析构释放,且它没有"必须全部归还"的断言。
}

void LatestFrameBuffer::commit(std::int64_t timestamp_ns) noexcept {
    // Stamp BEFORE the release exchange so the timestamp publishes with the pixels.
    // 时间戳在 release exchange 前打,使其与像素一起发布。
    m_slots[m_write_index].timestamp_ns = timestamp_ns;

    // Publish: release-store the write index (+dirty), get the old back as the next
    // write slot. acq_rel — the release half pushes all pixel writes to the consumer.
    // Wait-free: no branch waits on the consumer; we always get a slot back at once.
    // 发布:release 写入 write 索引(带 dirty),拿旧 back 当下一个 write slot。acq_rel——
    // release 半把全部像素写入推给消费者。wait-free:无任何等消费者的分支,立刻拿回一个槽。
    const std::uint32_t prev =
        m_back.exchange(static_cast<std::uint32_t>(m_write_index) | kDirty,
                        std::memory_order_acq_rel);

    // If the slot we just displaced still had dirty set, a published frame was
    // overwritten before the consumer took it → latest-wins drop.
    // 若刚换下的槽仍带 dirty,说明一帧已发布却没被取走就被覆盖 → latest-wins 丢帧。
    if (prev & kDirty)
        m_dropped.fetch_add(1, std::memory_order_relaxed);

    m_write_index = prev & kIndexMask;
}

std::optional<ImageFrame> LatestFrameBuffer::takeLatest() noexcept {
    // Gate: nothing committed since the last take → keep the consumer's current frame.
    // 闸门:自上次取帧后无提交 → 让消费者保留当前帧。
    if ((m_back.load(std::memory_order_acquire) & kDirty) == 0)
        return std::nullopt;

    // Take: release the read slot back (no dirty bit) and acquire the latest
    // committed slot. acq_rel — the acquire half guarantees we see every pixel
    // write the producer made before its release-exchange.
    // 取帧:把 read 槽换回去(不带 dirty),取最新已提交槽。acq_rel——acquire 半
    // 保证看到生产者在其 release-exchange 之前写下的全部像素。
    const std::uint32_t prev =
        m_back.exchange(static_cast<std::uint32_t>(m_read_index),
                        std::memory_order_acq_rel);

    m_read_index = prev & kIndexMask;
    return m_slots[m_read_index];
}

} // namespace indusscope::core
