#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "indusscope/core/FramePool.h"
#include "indusscope/core/ImageFrame.h"

namespace indusscope::core {

/// Lock-free, wait-free latest-wins triple buffer for cross-thread video handoff.
/// 无锁、wait-free 的"最新帧三缓冲",用于图像跨线程交接。
///
/// One producer thread (capture/decode) hands whole frames to one consumer thread
/// (render) with zero copy, zero blocking, no tearing. Strategy is latest-wins:
/// if the consumer falls behind, older frames are dropped — a live monitor must
/// show the freshest frame, never a backlog. This is the image lane's counterpart
/// to the curve lane's RingBuffer, but the opposite policy (ring keeps every
/// sample; this keeps only the latest). SPEC §5.1 (UI never blocks) / §5.3 (zero-copy).
/// 单生产线程(采集/解码)向单消费线程(渲染)零拷贝、零阻塞、无撕裂地整帧交接。
/// 策略 latest-wins:消费者跟不上就丢旧帧——实时监控只看最新帧,绝不积压。
/// 它是图像那一路对应曲线 RingBuffer 的地基,但策略相反(环攒每个采样,这里只留最新)。
///
/// ## SPSC contract / SPSC 契约
/// - Producer thread ONLY: writeSlot() + commit().  生产者线程只调 writeSlot()+commit()。
/// - Consumer thread ONLY: takeLatest().            消费者线程只调 takeLatest()。
/// - Roles MUST NOT be mixed across threads — doing so is a data race.
///   角色绝不可跨线程混用——违反即数据竞争。
///
/// ## Triple-buffer invariant / 三缓冲不变式
/// Three slot indices — write (producer-private), read (consumer-private), back
/// (the atomic) — are ALWAYS a permutation of {0,1,2}. The producer only swaps
/// {write,back}; the consumer only swaps {read,back}. So write != read always:
/// the two threads never touch the same slot → no tearing source.
/// 三个槽索引——write(生产者私有)、read(消费者私有)、back(原子)——恒为
/// {0,1,2} 的一个排列。生产者只 swap {write,back},消费者只 swap {read,back},
/// 故 write != read 恒成立:两线程绝不碰同一槽 → 无撕裂源头。
///
/// ## Memory ordering / 内存序
/// Producer fills ALL pixels, then publishes the index with release; consumer
/// acquires the index, so it is guaranteed to see every pixel write (happens-before).
/// relaxed would pass on x86's strong ordering yet let the consumer read a
/// half-written frame on weak-ordered ARM (tearing) — the classic x86-green /
/// ARM-red trap. Both swaps use acq_rel exchange, covering publish and take.
/// 生产者先写满所有像素,再用 release 发布索引;消费者 acquire 读索引,必看到全部
/// 像素写入(happens-before)。relaxed 在 x86 强序蒙混,ARM 弱序下让消费者读到
/// 半写帧(撕裂)——x86 绿 ARM 红的活靶子。两处换手都用 acq_rel exchange,
/// 同时覆盖发布与取帧。
class LatestFrameBuffer {
public:
    /// Construct a triple buffer of @p width × @p height @p format frames.
    /// Pre-allocates all 3 slots once; steady state never touches the pool again.
    /// 构造 @p width × @p height @p format 帧的三缓冲。一次性预分配 3 槽;
    /// 稳态绝不再碰池。
    LatestFrameBuffer(std::int32_t width, std::int32_t height,
                      PixelFormat format = PixelFormat::RGBA8888);

    // --- Producer side (producer thread ONLY) 生产者侧(仅生产线程) ---

    /// The slot the producer should paint into. Stable pointer until commit().
    /// 生产者当前应写入的 slot。commit() 前指针稳定。
    ImageFrame& writeSlot() noexcept { return m_slots[m_write_index]; }

    /// Stamp the write slot with @p timestamp_ns and publish it as the latest
    /// frame. Wait-free: always swaps in a fresh write slot, never waits for the
    /// consumer. If the previously published frame was never taken, it is dropped
    /// (latest-wins) and dropped() is incremented.
    /// 给 write slot 打上 @p timestamp_ns 并发布为最新帧。wait-free:总能立刻换到一个
    /// 新 write slot,绝不等消费者。若上一已发布帧没被取走则丢弃(latest-wins),
    /// dropped() 自增。
    void commit(std::int64_t timestamp_ns) noexcept;

    // --- Consumer side (consumer thread ONLY) 消费者侧(仅消费线程) ---

    /// Take the freshest committed frame, or std::nullopt when no frame arrived
    /// since the last take (caller should keep showing its current frame). The
    /// returned view is valid only until the NEXT takeLatest() call.
    /// 取最新已提交帧;自上次取帧后无帧到达则返回 std::nullopt(调用方应继续显示当前帧)。
    /// 返回的视图仅在下次 takeLatest() 调用前有效。
    std::optional<ImageFrame> takeLatest() noexcept;

    // --- Observers 观察者 ---
    std::int32_t  width()   const noexcept { return m_pool.width(); }
    std::int32_t  height()  const noexcept { return m_pool.height(); }
    std::int32_t  stride()  const noexcept { return m_pool.stride(); }
    PixelFormat   format()  const noexcept { return m_pool.format(); }

    /// Count of published frames overwritten before the consumer took them.
    /// 已发布但被消费者取走前即被覆盖的帧数。
    std::uint64_t dropped() const noexcept { return m_dropped.load(std::memory_order_relaxed); }

private:
    // Atomic bit layout / 原子位布局:bits 0-1 = slot index, bit 2 = fresh-frame flag.
    static constexpr std::uint32_t kIndexMask = 0x3u; // slot index 0..2 / 槽索引 0..2
    static constexpr std::uint32_t kDirty     = 0x4u; // a fresh frame is ready / 有新帧就绪

    FramePool                 m_pool;        // owns the 3 slots' storage / 持有 3 槽存储
    std::array<ImageFrame, 3> m_slots;       // fixed views, juggled by index only / 固定视图,仅倒索引
    std::size_t               m_write_index; // producer-private / 生产者私有
    std::size_t               m_read_index;  // consumer-private / 消费者私有
    std::atomic<std::uint32_t> m_back;       // shared: index of latest committed + dirty / 共享:最新就绪槽索引 + dirty
    std::atomic<std::uint64_t> m_dropped{0}; // latest-wins drop counter / latest-wins 丢帧计数
};

// 30 fps single-atomic handoff: the index atomic must be truly lock-free or the
// "wait-free" claim is a lie on the target. / 30fps 单原子交接:索引原子必须真无锁,
// 否则目标平台上的 "wait-free" 是假话。
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "LatestFrameBuffer needs a lock-free 32-bit atomic index / "
              "LatestFrameBuffer 需要无锁的 32 位原子索引");

} // namespace indusscope::core
