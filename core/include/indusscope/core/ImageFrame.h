#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace indusscope::core {

/// Pixel format of an image frame; byte count per pixel is fixed per value.
/// 图像帧像素格式;每个取值对应固定的每像素字节数。
enum class PixelFormat : std::uint8_t {
    RGBA8888 = 0,  // 4 B/px, default — GPU-native, no stride padding / 默认——GPU 原生、无行填充
    RGB888,        // 3 B/px, placeholder for real camera (S2.6c) / 占位,留给真相机切片
    Grayscale8,    // 1 B/px, placeholder for industrial mono camera / 占位,工业 mono 相机
};

/// Bytes per pixel for @p f.
/// 返回格式 @p f 的每像素字节数。
inline constexpr std::int32_t bytes_per_pixel(PixelFormat f) noexcept {
    switch (f) {
        case PixelFormat::RGBA8888:   return 4;
        case PixelFormat::RGB888:     return 3;
        case PixelFormat::Grayscale8: return 1;
    }
    return 0;
}

/// Bytes per row, rounded up to a 4-byte boundary.
/// 每行字节数,向上取整到 4 字节边界。
///
/// QImage(uchar*, w, h, bytesPerLine, fmt) requires 4-byte-aligned scanlines;
/// pinning the rule here keeps S2.6b zero-copy wrapping free of rework.
/// QImage 要求扫描线 4 字节对齐;在此钉死省得 S2.6b 零拷贝包帧时返工。
/// (RGBA8888: stride == width*4 already satisfies it; field reserved for grayscale.)
/// (RGBA8888 下 stride 自动满足;字段为以后 grayscale 预留。)
inline constexpr std::int32_t aligned_stride(std::int32_t width, PixelFormat f) noexcept {
    // Compute in size_t to avoid int32 overflow on huge widths / 中间值用 size_t,防超大宽度溢出
    const std::size_t raw = static_cast<std::size_t>(width) * static_cast<std::size_t>(bytes_per_pixel(f));
    return static_cast<std::int32_t>((raw + 3) & ~static_cast<std::size_t>(3));
}

/// Lightweight non-owning view of one pooled image frame: metadata + a pointer
/// into pool-owned bytes. Trivially copyable — pass by value copies only the
/// pointer + metadata, never the pixels.
/// 轻量非拥有图像帧视图:元数据 + 指向池内字节的指针。平凡可拷贝——
/// 按值传只搬指针+元数据,绝不搬像素。
///
/// Lifetime / 生命周期:
/// - `data` points into the FramePool that produced it; the frame is a borrow.
///   `data` 指向产出它的 FramePool;本帧只是借用。
/// - After FramePool::release(frame), the frame MUST NOT be accessed again.
///   调用 FramePool::release(frame) 之后,不得再访问该帧。
struct ImageFrame {
    /// Non-owning pointer to the frame's first byte, inside the pool.
    /// 非拥有指针,指向池内该帧首字节。
    std::byte* data;

    /// Capture timestamp (steady_clock nanoseconds); end-to-end p99 probe.
    /// 采集时刻 (steady_clock 纳秒);端到端 p99 埋点口。
    std::int64_t timestamp_ns;

    /// Frame geometry / 帧几何。
    std::int32_t width;
    std::int32_t height;

    /// Bytes per row (>= width*bytes_per_pixel, 4-byte aligned).
    /// 每行字节数 (>= width*每像素字节数,4 字节对齐)。
    std::int32_t stride;

    /// Pixel format / 像素格式。
    PixelFormat format;
};

static_assert(std::is_trivially_copyable_v<ImageFrame>,
              "ImageFrame must be trivially copyable: pass-by-value copies pointer + metadata only / "
              "ImageFrame 必须可平凡拷贝:按值传只搬指针+元数据");

} // namespace indusscope::core
