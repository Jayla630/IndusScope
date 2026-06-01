#pragma once

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

/// Single-thread ring buffer with reject-on-full policy.
/// 单线程环形缓冲,满则拒绝策略。
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
/// Upgrade path for S2.2 lock-free SPSC: replace `m_head` / `m_tail` with
/// `std::atomic<std::size_t>` + acquire/release ordering — public API unchanged.
/// 升级到 S2.2 无锁 SPSC 的路径:将 `m_head` / `m_tail` 替换为
/// `std::atomic<std::size_t>` + acquire/release 顺序——公开 API 不变。
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
    bool push(const T& item) {
        if (full()) {
            ++m_dropped;
            return false;
        }
        m_buf[m_head & m_mask] = item;
        ++m_head;
        return true;
    }

    /// Pop one item into @p out.  Returns false when empty.
    /// 弹出一个元素到 @p out。空时返回 false。
    bool pop(T& out) {
        if (empty())
            return false;
        out = m_buf[m_tail & m_mask];
        ++m_tail;
        return true;
    }

    /// Bulk-pop up to @p max_n items into caller-supplied buffer @p dst.
    /// 批量弹出最多 @p max_n 个元素到调用方提供的缓冲区 @p dst。
    /// Returns the number of items actually copied (may be less than max_n
    /// if the buffer holds fewer items).
    /// 返回实际拷贝的元素个数 (缓冲区中元素不足时可能少于 max_n)。
    std::size_t pop_batch(T* dst, std::size_t max_n) {
        std::size_t avail = m_head - m_tail;
        std::size_t count = (max_n < avail) ? max_n : avail;
        for (std::size_t i = 0; i < count; ++i) {
            dst[i] = m_buf[(m_tail + i) & m_mask];
        }
        m_tail += count;
        return count;
    }

    // --- Observers 观察者 ---

    std::size_t size()     const noexcept { return m_head - m_tail; }
    std::size_t capacity() const noexcept { return m_buf.size(); }
    bool        empty()    const noexcept { return m_head == m_tail; }
    bool        full()     const noexcept { return size() == capacity(); }
    std::size_t dropped()  const noexcept { return m_dropped; }

private:
    std::vector<T> m_buf;       // storage allocated once, never reallocated / 存储空间一次性分配,之后绝不重分配
    std::size_t    m_mask;      // = capacity() - 1, used for bitwise modulo / 用于位与取模
    std::size_t    m_head{0};   // write cursor, monotonically increasing / 写游标,单调递增
    std::size_t    m_tail{0};   // read cursor, monotonically increasing / 读游标,单调递增
    std::size_t    m_dropped{0}; // count of items rejected due to full / 因满被拒的元素计数
};

} // namespace indusscope::core
