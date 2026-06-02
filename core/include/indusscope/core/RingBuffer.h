#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace indusscope::core {

/// Round n up to the next power of 2 (n >= 1).
/// 将 n 向上取整到下一个 2 的幂 (n >= 1)。
///
/// Use bit-twiddling for portability — avoids builtins like __builtin_clz.
/// 用位运算实现跨平台兼容——避免 __builtin_clz 等编译器内建函数。
inline constexpr std::size_t next_pow2(std::size_t n) noexcept {
    if (n <= 1) return 1;
    --n;
    for (unsigned shift = 1; shift < sizeof(std::size_t) * 8; shift <<= 1)
        n |= n >> shift;
    return n + 1;
}

/// Lock-free SPSC (Single-Producer Single-Consumer) ring buffer with reject-on-full policy.
/// 无锁 SPSC（单生产者单消费者）环形缓冲,满则拒绝策略。
///
/// ## SPSC contract / SPSC 使用契约
///
/// - Producer thread(s) must ONLY call `push()`.  生产者线程必须只调 `push()`。
/// - Consumer thread(s) must ONLY call `pop()` / `pop_batch()`.  消费者线程必须只调 `pop()` / `pop_batch()`。
/// - Roles MUST NOT be mixed across threads — doing so is a data race.
///   角色绝不可跨线程混用——违反即为数据竞争。
/// - Observers (`size()`, `empty()`, `full()`, `dropped()`) may be called from either side
///   but return approximate values under concurrency (conservatively safe — see inline notes).
///   观察者可从任意侧调用,但在并发下返回近似值（保守安全——见行内注释）。
///
/// ## Memory ordering / 内存序四对配对
///
///   🅑 Producer    release-store m_head  ←→  Consumer acquire-load m_head  🅒  (data published / 数据发布)
///   🅓 Consumer    release-store m_tail  ←→  Producer acquire-load m_tail  🅐  (slot released / 槽释放)
///
///   Dropped-count uses relaxed everywhere — pure counter, no ordering needed.
///   丢点计数全程 relaxed——纯计数,无需与其他操作排序。
///
/// Storage is allocated once at construction (std::vector<T>(pow2_capacity))
/// and never reallocated — safe for high-frequency sampling paths.
/// 存储空间在构造时一次性分配 (std::vector<T>(pow2_capacity)),
/// 之后绝不重分配——适配高频采样路径。
///
/// Cursors (`m_head` / `m_tail`) are monotonically increasing so that
/// `size() = m_head - m_tail` unambiguously.  Indexing uses `& m_mask`
/// (power-of-2) instead of expensive modulo.
/// 游标 (`m_head` / `m_tail`) 单调递增,因此 `size() = m_head - m_tail`
/// 无歧义。索引用 `& m_mask` (2 的幂) 代替昂贵的模运算。
///
/// S2.2 upgrade executed: `m_head` / `m_tail` / `m_dropped` are now
/// `std::atomic<std::size_t>` with acquire/release memory ordering.
/// Public API unchanged from the single-threaded version.
/// S2.2 升级已完成:`m_head` / `m_tail` / `m_dropped` 已改为
/// `std::atomic<std::size_t>` + acquire/release 内存序。公开 API 不变。
template <typename T>
class RingBuffer {
public:
    /// Construct a ring buffer with *at least* @p capacity slots.
    /// 构造一个至少含 @p capacity 个槽位的环形缓冲。
    /// Actual capacity is rounded up to the next power of 2.
    /// 实际容量向上取整到下一个 2 的幂。
    explicit RingBuffer(std::size_t capacity)
        : m_buf(next_pow2(capacity))
        , m_mask(m_buf.size() - 1)
    {
    }

    /// Push one item.  Returns false and increments dropped() when full.
    /// 推入一个元素。满时返回 false 并递增 dropped()。
    /// Called by the producer thread ONLY.  仅由生产者线程调用。
    bool push(const T& item) {
        // 1. relaxed-load own cursor — only need latest value / relaxed 读自己的游标——只需最新值
        std::size_t head = m_head.load(std::memory_order_relaxed);

        // 2. acquire-load consumer's tail — paired with consumer release-store m_tail (pair 🅓)
        //    acquire 读消费者的 tail——与消费者 release-store m_tail 配对 (对 🅓)
        std::size_t tail = m_tail.load(std::memory_order_acquire);

        if (head - tail == m_mask + 1) { // full / 满
            // relaxed — pure counter, nothing else to order / relaxed——纯计数,无需排序
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        // 3. plain write — this slot belongs exclusively to the producer / 普通写——此刻该槽独属生产者
        m_buf[head & m_mask] = item;

        // 4. release-store head — paired with consumer acquire-load m_head (pair 🅑)
        //    release-store head——与消费者 acquire-load m_head 配对 (对 🅑)
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    /// Pop one item into @p out.  Returns false when empty.
    /// 弹出一个元素到 @p out。空时返回 false。
    /// Called by the consumer thread ONLY.  仅由消费者线程调用。
    bool pop(T& out) {
        // 1. relaxed-load own cursor — only need latest value / relaxed 读自己的游标——只需最新值
        std::size_t tail = m_tail.load(std::memory_order_relaxed);

        // 2. acquire-load producer's head — paired with producer release-store m_head (pair 🅑)
        //    acquire 读生产者的 head——与生产者 release-store m_head 配对 (对 🅑)
        std::size_t head = m_head.load(std::memory_order_acquire);

        if (head == tail) // empty / 空
            return false;

        // 3. plain read — data already published via release-store m_head / 普通读——数据已通过 release-store m_head 发布
        out = m_buf[tail & m_mask];

        // 4. release-store tail — paired with producer acquire-load m_tail (pair 🅓)
        //    release-store tail——与生产者 acquire-load m_tail 配对 (对 🅓)
        m_tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    /// Bulk-pop up to @p max_n items into caller-supplied buffer @p dst.
    /// 批量弹出最多 @p max_n 个元素到调用方提供的缓冲区 @p dst。
    /// Returns the number of items actually copied (may be less than max_n
    /// if the buffer holds fewer items).
    /// 返回实际拷贝的元素个数 (缓冲区中元素不足时可能少于 max_n)。
    /// Called by the consumer thread ONLY.  仅由消费者线程调用。
    std::size_t pop_batch(T* dst, std::size_t max_n) {
        // 1. relaxed-load own cursor / relaxed 读自己的游标
        std::size_t tail = m_tail.load(std::memory_order_relaxed);

        // 2. acquire-load producer's head — paired with producer release-store m_head (pair 🅑)
        //    acquire 读生产者的 head——与生产者 release-store m_head 配对 (对 🅑)
        std::size_t head = m_head.load(std::memory_order_acquire);

        std::size_t avail = head - tail;
        std::size_t count = (max_n < avail) ? max_n : avail;
        if (count == 0) return 0;

        // 3. plain reads — data already published via release-store m_head / 普通读——数据已发布
        for (std::size_t i = 0; i < count; ++i) {
            dst[i] = m_buf[(tail + i) & m_mask];
        }

        // 4. release-store tail (once at end) — paired with producer acquire-load m_tail (pair 🅓)
        //    末尾一次性 release-store tail——与生产者 acquire-load m_tail 配对 (对 🅓)
        m_tail.store(tail + count, std::memory_order_release);
        return count;
    }

    // --- Observers 观察者 ---
    // All relaxed loads — stale values are conservatively safe under concurrency:
    // 全部 relaxed 读——并发下取值可能陈旧,但倾向保守安全:
    //   size() lowball → consumer sees less data (empty-read is safe) / size() 偏小 → 消费者以为数据更少 (空读安全)
    //   full() highball → producer rejects earlier (reject-on-full is safe) / full() 偏"满" → 生产者更早拒绝 (拒满安全)
    //   dropped() may lag but is monotonically increasing / dropped() 可能滞后但单调递增

    std::size_t size()     const noexcept { return m_head.load(std::memory_order_relaxed) - m_tail.load(std::memory_order_relaxed); }
    std::size_t capacity() const noexcept { return m_buf.size(); }
    bool        empty()    const noexcept { return m_head.load(std::memory_order_relaxed) == m_tail.load(std::memory_order_relaxed); }
    bool        full()     const noexcept { return size() == capacity(); }
    std::size_t dropped()  const noexcept { return m_dropped.load(std::memory_order_relaxed); }

private:
    std::vector<T>               m_buf;       // storage allocated once, never reallocated / 存储空间一次性分配,之后绝不重分配
    std::size_t                  m_mask;      // = capacity() - 1, used for bitwise modulo / 用于位与取模
    std::atomic<std::size_t>     m_head{0};   // write cursor, monotonically increasing / 写游标,单调递增
    std::atomic<std::size_t>     m_tail{0};   // read cursor, monotonically increasing / 读游标,单调递增
    std::atomic<std::size_t>     m_dropped{0}; // count of items rejected due to full / 因满被拒的元素计数
};

} // namespace indusscope::core
