#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "indusscope/core/ImageFrame.h"

namespace indusscope::core {

/// Fixed-capacity image-frame pool: one-shot pre-allocation at construction,
/// zero allocation on the acquire/release hot path, free-list reuse.
/// 定容图像帧池:构造期一次性预分配,acquire/release 热路径零分配,空闲表复用。
///
/// This is the image lane's analogue of RingBuffer (curve lane). It exists to
/// kill per-frame new/delete: 1080p ≈ 8 MB/frame, 30 fps ≈ 240 MB/s would
/// fragment the heap and cause latency spikes (SPEC §5.3 iron law).
/// 这是图像那一路对应曲线 RingBuffer 的地基,杜绝每帧 new/delete:
/// 1080p≈8MB/帧、30fps≈240MB/s,malloc/free 会碎片化并造成延迟尖刺(SPEC §5.3 铁律)。
///
/// ## Single-threaded contract / 单线程契约
/// acquire/release are NOT thread-safe — no atomics here. The lock-free
/// triple-buffer index is S2.6b's job.
/// acquire/release 非线程安全——本刀不上原子。无锁三缓冲索引是 S2.6b 的活。
///
/// ## Slot contents are undefined / 槽内容未定义
/// acquire() does NOT zero/memset the slot. The returned slot's contents are
/// undefined; the caller MUST fully overwrite the frame before use.
/// acquire() 不清零、不 memset。返回的 slot 内容未定义,使用方用前必须整帧写满。
class FramePool {
public:
    /// Construct a pool of @p slot_count buffers, each holding one
    /// @p width × @p height frame in @p format. Geometry/format/capacity are
    /// fixed at construction — no runtime resize (change geometry = rebuild pool).
    /// 构造含 @p slot_count 个缓冲的池,每个容纳一帧 @p width × @p height 的
    /// @p format 图像。几何/格式/容量构造期定死——无运行期扩容(改几何=重建池)。
    FramePool(std::int32_t width, std::int32_t height, PixelFormat format, std::size_t slot_count);

    /// Acquire a free slot as an ImageFrame, stamped with @p timestamp_ns.
    /// Returns std::nullopt when all slots are in use (no allocation, no crash).
    /// 取一个空闲 slot 作为 ImageFrame,打上 @p timestamp_ns 时间戳。
    /// 所有 slot 占满时返回 std::nullopt(不分配、不崩)。
    std::optional<ImageFrame> acquire(std::int64_t timestamp_ns = 0);

    /// Return a previously acquired frame to the pool. Debug-asserts the pointer
    /// is a valid, not-already-freed slot (program error otherwise).
    /// 归还一个先前 acquire 的帧。debug 下断言指针为合法且未重复归还的 slot
    /// (否则属程序员误用)。
    void release(const ImageFrame& frame);

    // --- Observers 观察者 ---
    std::size_t  capacity()  const noexcept { return m_slot_count; }
    std::size_t  available() const noexcept { return m_free_top; }
    std::size_t  in_use()    const noexcept { return m_slot_count - m_free_top; }
    std::int32_t width()     const noexcept { return m_width; }
    std::int32_t height()    const noexcept { return m_height; }
    std::int32_t stride()    const noexcept { return m_stride; }
    PixelFormat  format()    const noexcept { return m_format; }

private:
    std::int32_t m_width;
    std::int32_t m_height;
    std::int32_t m_stride;      // bytes per row, 4-byte aligned / 每行字节数,4 字节对齐
    PixelFormat  m_format;
    std::size_t  m_slot_count;
    std::size_t  m_slot_bytes;  // stride * height, in size_t to avoid overflow / 用 size_t 防溢出

    std::vector<std::byte>   m_storage; // one contiguous block, allocated once / 单块连续存储,一次性分配

    // Free-slot stack as a fixed-size array + top index — no push_back/pop_back,
    // so storage is never reallocated on the acquire/release path.
    // 空闲槽栈 = 定长数组 + 栈顶索引——不用 push_back/pop_back,
    // 故 acquire/release 路径绝不重分配存储。
    std::vector<std::size_t> m_free;     // size == slot_count, holds free slot indices in [0, m_free_top) / 大小恒为 slot_count,[0, m_free_top) 为空闲槽索引
    std::size_t              m_free_top; // count of free slots = stack size / 空闲槽数 = 栈大小
};

} // namespace indusscope::core
