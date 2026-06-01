#pragma once

#include <cstddef>
#include <vector>

namespace indusscope::core {

/// Round n up to the next power of 2 (n >= 1).
/// Use bit-twiddling for portability — avoids builtins like __builtin_clz.
inline constexpr std::size_t next_pow2(std::size_t n) noexcept {
    if (n <= 1) return 1;
    --n;
    for (unsigned shift = 1; shift < sizeof(std::size_t) * 8; shift <<= 1)
        n |= n >> shift;
    return n + 1;
}

/// Single-thread ring buffer with reject-on-full policy.
///
/// Storage is allocated once at construction (std::vector<T>(pow2_capacity))
/// and never reallocated — safe for high-frequency sampling paths.
///
/// Cursors (`m_head` / `m_tail`) are monotonically increasing so that
/// `size() = m_head - m_tail` unambiguously.  Indexing uses `& m_mask`
/// (power-of-2) instead of expensive modulo.
///
/// Upgrade path for S2.2 lock-free SPSC: replace `m_head` / `m_tail` with
/// `std::atomic<std::size_t>` + acquire/release ordering — public API unchanged.
template <typename T>
class RingBuffer {
public:
    /// Construct a ring buffer with *at least* @p capacity slots.
    /// Actual capacity is rounded up to the next power of 2.
    explicit RingBuffer(std::size_t capacity)
        : m_buf(next_pow2(capacity))
        , m_mask(m_buf.size() - 1)
    {
    }

    /// Push one item.  Returns false and increments dropped() when full.
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
    bool pop(T& out) {
        if (empty())
            return false;
        out = m_buf[m_tail & m_mask];
        ++m_tail;
        return true;
    }

    /// Bulk-pop up to @p max_n items into caller-supplied buffer @p dst.
    /// Returns the number of items actually copied (may be less than max_n
    /// if the buffer holds fewer items).
    std::size_t pop_batch(T* dst, std::size_t max_n) {
        std::size_t avail = m_head - m_tail;
        std::size_t count = (max_n < avail) ? max_n : avail;
        for (std::size_t i = 0; i < count; ++i) {
            dst[i] = m_buf[(m_tail + i) & m_mask];
        }
        m_tail += count;
        return count;
    }

    // --- Observers ---

    std::size_t size()     const noexcept { return m_head - m_tail; }
    std::size_t capacity() const noexcept { return m_buf.size(); }
    bool        empty()    const noexcept { return m_head == m_tail; }
    bool        full()     const noexcept { return size() == capacity(); }
    std::size_t dropped()  const noexcept { return m_dropped; }

private:
    std::vector<T> m_buf;       // storage allocated once, never reallocated
    std::size_t    m_mask;      // = capacity() - 1, used for bitwise modulo
    std::size_t    m_head{0};   // write cursor, monotonically increasing
    std::size_t    m_tail{0};   // read cursor, monotonically increasing
    std::size_t    m_dropped{0}; // count of items rejected due to full
};

} // namespace indusscope::core
