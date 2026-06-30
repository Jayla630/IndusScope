#pragma once

#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

#include <optional>

#include "indusscope/core/ImageFrame.h"
#include "indusscope/core/SyntheticFrameSource.h"

class QTimer;

namespace indusscope::ui {

/// Custom QQuickItem that puts synthetic frames on screen — the image lane's
/// analogue of the curve lane's CurveController. Single-threaded "first light":
/// a GUI-thread timer drives core::SyntheticFrameSource into a pool slot, and
/// updatePaintNode wraps those bytes zero-copy into a QImage, uploads them via
/// createTextureFromImage, and hangs the result on a QSGSimpleTextureNode.
/// 把合成帧弄上屏的自定义 QQuickItem——图像那一路对应曲线 CurveController。
/// 单线程"首光":GUI 线程定时器驱动 core::SyntheticFrameSource 画进池 slot,
/// updatePaintNode 用 QImage 零拷贝包那些字节,经 createTextureFromImage 上传,
/// 挂到 QSGSimpleTextureNode 上。
///
/// Architecture / 架构:FrameView lives in ui (Qt-dependent) and uses core's
/// SyntheticFrameSource/paintTestPattern to produce frames — ★ core stays Qt-free.
/// This slice (S2.6b-2) does NOT touch worker threads or the triple buffer (b-3),
/// nor a real camera (S2.6c).
/// FrameView 在 ui(依赖 Qt),用 core 的 SyntheticFrameSource/paintTestPattern 产帧
/// ——★core 仍零 Qt。本刀(S2.6b-2)不接 worker/三缓冲(b-3),不接真相机(S2.6c)。
///
/// Construction only assembles members; QML must call start() explicitly to begin
/// the frame pump — no hidden side effects in the constructor (mirrors CurveController).
/// 构造仅装配成员;QML 须显式调用 start() 启动帧泵——构造函数无隐藏副作用(对齐 CurveController)。
class FrameView : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT

    /// Source frame width in pixels; settable from QML before start() (default 1280).
    /// 源帧宽(像素);start() 前可由 QML 设(默认 1280)。
    Q_PROPERTY(int frameWidth READ frameWidth WRITE setFrameWidth NOTIFY frameWidthChanged FINAL)

    /// Source frame height in pixels; settable from QML before start() (default 720).
    /// 源帧高(像素);start() 前可由 QML 设(默认 720)。
    Q_PROPERTY(int frameHeight READ frameHeight WRITE setFrameHeight NOTIFY frameHeightChanged FINAL)

public:
    /// Construct and assemble the frame-pump timer. The timer is NOT started —
    /// QML must call start() explicitly. / 构造并装配帧泵定时器。timer 不会启动——QML 须显式 start()。
    explicit FrameView(QQuickItem* parent = nullptr);

    /// Destructor defined in .cpp (where the complete SyntheticFrameSource is visible).
    /// 析构函数在 .cpp 中定义(SyntheticFrameSource 完整可见处)。
    ~FrameView() override;

    // --- Property accessors 属性访问器 ---
    int frameWidth()  const noexcept { return m_frameWidth; }
    int frameHeight() const noexcept { return m_frameHeight; }
    void setFrameWidth(int w);
    void setFrameHeight(int h);

public slots:
    /// Start the frame pump. Idempotent — no-op if already running. (Re)builds the
    /// source at the current geometry. / 启动帧泵。幂等——已运行时为空操作。按当前几何(重)建源。
    void start();

    /// Stop the frame pump. Idempotent — no-op if already stopped.
    /// 停止帧泵。幂等——已停止时为空操作。
    void stop();

signals:
    void frameWidthChanged();
    void frameHeightChanged();

protected:
    /// ★ ALL scene-graph / texture operations live ONLY here (render thread, GUI
    /// blocked). Reuses oldNode; first call builds the node + setOwnsTexture(true);
    /// thereafter each dirty frame builds a fresh texture and the node auto-deletes
    /// the old one. / ★所有场景图/纹理操作只在此(渲染线程,GUI 阻塞)。复用 oldNode;
    /// 首次建 node + setOwnsTexture(true);此后每张脏帧建新纹理,旧的由 node 自动删。
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

    /// Connect the fps counter once the item gains a window (GUI thread). window() is
    /// not reliably set yet at Component.onCompleted, so connecting in start() can no-op.
    /// 一旦 item 获得 window 即接 fps 计数(GUI 线程)。Component.onCompleted 时 window()
    /// 未必就绪,在 start() 里接可能空连接。
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private slots:
    /// Frame-pump tick (~33 ms, GUI thread): recycle previous frame, produce the
    /// next via SyntheticFrameSource, mark dirty, schedule update().
    /// 帧泵滴答(~33ms,GUI 线程):归还上一帧,经 SyntheticFrameSource 产下一帧,置脏,安排 update()。
    void onTick();

private:
    // --- Constants 常量 ---
    static constexpr int kTimerIntervalMs = 33;   // ~30 fps frame pump / ~30fps 帧泵
    static constexpr int kFpsReportMs     = 1000; // fps log cadence / fps 打印节奏

    // --- Timers 定时器 (Qt parent-child ownership, RAII) ---
    QTimer* m_timer    = nullptr; // frame pump / 帧泵
    QTimer* m_fpsTimer = nullptr; // 1 s fps reporter / 每秒 fps 报告

    /// Frame source — built lazily in start() so geometry can be set from QML first.
    /// Owns its FramePool; ★ a pure-core type, keeping Qt out of core.
    /// 帧源——start() 中延迟构造,以便先由 QML 设几何。自持 FramePool;★纯 core 类型,Qt 不渗进 core。
    std::optional<indusscope::core::SyntheticFrameSource> m_source;

    /// The frame currently destined for the screen — borrows pool-owned bytes,
    /// valid until the next recycle()/take. / 当前要上屏的帧——借用池内字节,有效到下次 recycle()。
    indusscope::core::ImageFrame m_currentFrame{};
    bool m_haveFrame    = false; // m_currentFrame holds a live borrow / m_currentFrame 持有有效借用
    bool m_textureDirty = false; // a NEW frame arrived → updatePaintNode must rebuild the texture / 来了新帧 → updatePaintNode 须重建纹理

    int  m_frameWidth  = 1280;
    int  m_frameHeight = 720;
    bool m_running     = false;

    /// Rendered-frame counter, incremented on the GUI thread via a queued
    /// frameSwapped connection; reset each fps report. / 渲染帧计数,经 queued
    /// frameSwapped 连接在 GUI 线程自增;每次 fps 报告清零。
    int  m_frameCount    = 0;
    bool m_fpsConnected  = false; // frameSwapped connected once item has a window / item 有 window 后接一次 frameSwapped
};

} // namespace indusscope::ui
