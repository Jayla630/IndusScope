#pragma once

#include <cstdint>
#include <optional>

#include "indusscope/core/ImageFrame.h"

namespace indusscope::core {

/// Abstract image-frame source: pull one frame, recycle it when done.
/// 图像帧源抽象:拉一帧,用完归还。
///
/// Pull model, symmetric to IDeviceProtocol::poll on the curve lane: the
/// consumer drives cadence by calling nextFrame(); the source never spins a
/// clock or a callback thread of its own (hermetic). Maps cleanly onto real
/// camera drivers later (S2.6c) — nextFrame/recycle pair up with V4L2
/// DQBUF/QBUF, so the seam stays put.
/// 拉模型,对称曲线那一路的 IDeviceProtocol::poll:消费者调 nextFrame() 驱动节奏,
/// 源内绝不自起时钟或回调线程(hermetic)。日后真相机(S2.6c)正好对上——
/// nextFrame/recycle 对应 V4L2 的 DQBUF/QBUF,接缝不用改。
///
/// Borrow semantics / 借用语义:
/// - nextFrame() returns a non-owning view into source-owned bytes.
///   nextFrame() 返回指向源内字节的非拥有视图。
/// - After recycle(frame), the frame MUST NOT be accessed again.
///   recycle(frame) 之后,不得再访问该帧。
class IFrameSource {
public:
    virtual ~IFrameSource() = default;

    /// Pull the next frame, stamped with the caller-supplied @p timestamp_ns.
    /// Returns std::nullopt when the backing storage is full (no alloc, no crash).
    /// 拉下一帧,打上调用方传入的 @p timestamp_ns 时间戳。
    /// 底层存储占满时返回 std::nullopt(不分配、不崩)。
    virtual std::optional<ImageFrame> nextFrame(std::int64_t timestamp_ns) = 0;

    /// Return a frame previously obtained from nextFrame() to the source.
    /// 把先前从 nextFrame() 取得的帧归还给源。
    virtual void recycle(const ImageFrame& frame) = 0;
};

} // namespace indusscope::core
