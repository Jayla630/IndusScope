#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "indusscope/core/FramePool.h"
#include "indusscope/core/IFrameSource.h"
#include "indusscope/core/ImageFrame.h"

namespace indusscope::core {

/// Deterministic synthetic frame source: draws a moving test pattern into a
/// pool-owned slot every frame. The image lane's analogue of the curve lane's
/// SignalGenerator + MockSource — a clock-free, byte-assertable mock so S2.6b
/// can bring up the on-screen path before any real camera (S2.6c) exists.
/// 确定性合成帧源:每帧往池内 slot 画一幅移动测试图案。它是图像那一路对应
/// 曲线 SignalGenerator + MockSource 的 mock 源——不读时钟、逐字节可断言,
/// 让 S2.6b 在没有真相机(S2.6c)前就能把上屏路径打通。
///
/// Pattern = pure function of (frame_index, x, y):
/// 图案 = (frame_index, x, y) 的纯函数:
/// - background: static gradient R=x·255/(w-1), G=y·255/(h-1), B=0, A=255.
///   背景:静态渐变 R=x·255/(w-1),G=y·255/(h-1),B=0,A=255。
/// - moving bar: a kBarWidth-wide white column at col = frame_index % width.
///   移动竖条:列 = frame_index % width 处一条 kBarWidth 宽的白竖条。
///
/// Single-threaded; owns its FramePool (self-contained, hermetic). The pool
/// upgrades in place to S2.6b's triple buffer (default slot_count == 3).
/// 单线程;自持 FramePool(自包含、hermetic)。该池在 S2.6b 原地升级为
/// 三缓冲(默认 slot_count == 3)。
class SyntheticFrameSource final : public IFrameSource {
public:
    /// Width of the moving white bar, in pixels. Named, not a magic number —
    /// S2.6b demos may thicken it (then handle col+kBarWidth wrap past width;
    /// no wrap at 1px). / 移动白竖条宽(像素)。具名非魔数——S2.6b 录 demo 可能
    /// 加粗(届时再处理 col+kBarWidth 越过 width 的回绕;1px 无此问题)。
    static constexpr int kBarWidth = 1;

    /// Construct a source producing @p width × @p height RGBA8888 frames,
    /// backed by a pool of @p slot_count slots.
    /// 构造一个产出 @p width × @p height RGBA8888 帧的源,底层 @p slot_count 槽池。
    /// @pre width >= 2 && height >= 2 (gradient needs >= 2 px to span 0..255).
    ///      前置:width >= 2 && height >= 2(渐变需至少 2 像素铺满 0~255)。
    explicit SyntheticFrameSource(std::int32_t width = 320,
                                  std::int32_t height = 240,
                                  std::size_t slot_count = 3);

    /// Acquire a slot, paint frame frameIndex() into it, stamp @p timestamp_ns,
    /// then advance the frame counter. Returns nullopt when the pool is full —
    /// no frame painted, counter NOT advanced.
    /// 取一个 slot,把第 frameIndex() 帧画进去,打上 @p timestamp_ns,再推进帧计数。
    /// 池满时返回 nullopt——不画帧、计数不推进。
    std::optional<ImageFrame> nextFrame(std::int64_t timestamp_ns) override;

    /// Return a frame to the pool. / 把帧归还给池。
    void recycle(const ImageFrame& frame) override;

    // --- Observers 观察者 ---

    /// Monotonic counter of the NEXT frame to be produced (== frames produced
    /// so far). Symmetric to MockSource::index(). / 下一帧的单调编号
    /// (== 至今成功产出的帧数)。对称 MockSource::index()。
    std::uint64_t frameIndex() const noexcept { return m_frame_index; }

    /// Read-only access to the backing pool. / 底层池的只读访问。
    const FramePool& pool() const noexcept { return m_pool; }

private:
    FramePool     m_pool;           // source owns its pool / 源自持池
    std::uint64_t m_frame_index{0}; // drives bar position; advances per produced frame / 驱动竖条位置;每出一帧自增
};

} // namespace indusscope::core
