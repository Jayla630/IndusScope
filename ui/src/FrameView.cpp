#include "indusscope/ui/FrameView.h"

#include <QImage>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QTimer>
#include <QDebug>

#include <chrono>
#include <cstdint>

namespace indusscope::ui {

namespace {
/// steady_clock now in nanoseconds — the end-to-end timestamp stamped onto frames.
/// steady_clock 现在的纳秒值——打在帧上的端到端时间戳。
std::int64_t nowNs() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

FrameView::FrameView(QQuickItem* parent)
    : QQuickItem(parent)
    , m_timer(new QTimer(this))     // parent=this → Qt parent-child ownership, RAII / parent=this → Qt 父子所有权,RAII
    , m_fpsTimer(new QTimer(this))  // parent=this → Qt parent-child ownership, RAII / parent=this → Qt 父子所有权,RAII
{
    // ItemHasContents enables updatePaintNode() dispatch for this item.
    // ItemHasContents 启用本 item 的 updatePaintNode() 派发。
    setFlag(ItemHasContents, true);

    m_timer->setInterval(kTimerIntervalMs);
    // PreciseTimer — the default CoarseTimer coalesces to the ~15.6 ms Windows system
    // tick, snapping 33 ms up to 46.8 ms (≈21 fps). Precise keeps us at ~30 fps.
    // PreciseTimer——默认 CoarseTimer 会对齐到 ~15.6ms 的 Windows 系统 tick,把 33ms
    // 顶到 46.8ms(≈21fps)。Precise 才能稳在 ~30fps。
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &FrameView::onTick);

    m_fpsTimer->setInterval(kFpsReportMs);
    connect(m_fpsTimer, &QTimer::timeout, this, [this]() {
        qDebug() << "[FrameView] render fps:" << m_frameCount
                 << "produced:" << (m_source ? m_source->frameIndex() : 0);
        m_frameCount = 0;
    });
}

FrameView::~FrameView() = default;

void FrameView::setFrameWidth(int w) {
    // SyntheticFrameSource requires width >= 2; ignore illegal values silently.
    // SyntheticFrameSource 前置 width >= 2;非法值静默忽略。
    if (w < 2 || w == m_frameWidth)
        return;
    m_frameWidth = w;
    emit frameWidthChanged();
}

void FrameView::setFrameHeight(int h) {
    if (h < 2 || h == m_frameHeight)
        return;
    m_frameHeight = h;
    emit frameHeightChanged();
}

void FrameView::start() {
    if (m_running)  // idempotent / 幂等
        return;

    // Optional env override for resolution baselining (e.g. 1080p) without editing QML.
    // 可选 env 覆盖分辨率,便于基线(如 1080p)无需改 QML。
    const int envW = qEnvironmentVariableIntValue("INDUSSCOPE_FRAME_W");
    const int envH = qEnvironmentVariableIntValue("INDUSSCOPE_FRAME_H");
    if (envW >= 2) m_frameWidth  = envW;
    if (envH >= 2) m_frameHeight = envH;

    // (Re)build the source at the current geometry. ★ Qt stays out of core.
    // 按当前几何(重)建源。★Qt 不渗进 core。
    m_source.emplace(m_frameWidth, m_frameHeight, /*slot_count=*/3);
    m_haveFrame = false;
    m_running   = true;

    m_timer->start();
    m_fpsTimer->start();
}

void FrameView::stop() {
    if (!m_running)  // idempotent / 幂等
        return;
    m_timer->stop();
    m_fpsTimer->stop();
    m_running = false;
}

void FrameView::itemChange(ItemChange change, const ItemChangeData& value) {
    // fps counter — frameSwapped fires on the RENDER thread; binding to `this` as the
    // context object forces a queued connection onto the GUI thread, so m_frameCount is
    // only ever touched from one thread (avoids a hidden GUI/render write race).
    // fps 计数——frameSwapped 由渲染线程发出;用 `this` 作 context object 强制 queued
    // connection 落到 GUI 线程,使 m_frameCount 只被单线程触碰(避免 GUI/渲染 写竞争隐患)。
    if (change == ItemSceneChange && value.window && !m_fpsConnected) {
        connect(value.window, &QQuickWindow::frameSwapped, this, [this]() { ++m_frameCount; });
        m_fpsConnected = true;
    }
    QQuickItem::itemChange(change, value);
}

void FrameView::onTick() {
    if (!m_source)
        return;

    // Single-threaded: recycle the previous borrow, then produce the next frame.
    // in_use stays at 1 → effectively one buffer repainted in place each tick.
    // 单线程:先归还上一帧借用,再产下一帧。in_use 恒为 1 → 每滴答等价一块缓冲原地重画。
    if (m_haveFrame) {
        m_source->recycle(m_currentFrame);
        m_haveFrame = false;
    }

    auto f = m_source->nextFrame(nowNs());
    if (f) {  // guard: never dereference an empty optional (UB) / 守卫:绝不解引用空 optional(UB)
        m_currentFrame = *f;
        m_haveFrame    = true;
        m_textureDirty = true;  // a new frame → rebuild the texture next paint / 新帧 → 下次 paint 重建纹理
        update();               // schedule updatePaintNode on the render thread / 安排渲染线程跑 updatePaintNode
    }
}

QSGNode* FrameView::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* /*data*/) {
    // ★ ALL QSG / texture operations live ONLY here (render thread, GUI blocked).
    // ★ 所有 QSG/纹理操作只在此(渲染线程,GUI 阻塞)。
    if (!m_haveFrame) {
        // No frame produced yet — return without building a textureless node, else the
        // renderer warns "No QSGTexture provided from updateSampledImage()".
        // 还没产出帧——不建无纹理 node 直接返回,否则渲染器会警告 "No QSGTexture provided"。
        return oldNode;
    }

    auto* node = static_cast<QSGSimpleTextureNode*>(oldNode);
    if (!node) {
        // Scene-graph owns this node and deletes it — the same accepted ownership
        // pattern as `new QTimer(this)` above, not a bare-new leak.
        // 场景图持有并删除该 node——与上面 `new QTimer(this)` 同款公认所有权模式,非裸 new 泄漏。
        node = new QSGSimpleTextureNode;
        node->setOwnsTexture(true);  // ★ texture lifetime delegated to node; never manual-delete / ★纹理生命周期交给 node;绝不手动 delete
        node->setFiltering(QSGTexture::Linear);
        m_textureDirty = true;       // a fresh node must get a texture this pass / 新建 node 本趟必须拿到纹理
    }

    if (m_textureDirty) {
        // Zero-copy wrap of pool bytes — bytesPerLine = frame.stride (NOT width*4, to
        // stay correct when grayscale arrives later). Format_RGBA8888 matches our
        // R,G,B,A memory order; RGB32 would be BGRA on little-endian = swapped R/B.
        // 零拷贝包池字节——bytesPerLine = frame.stride(非 width*4,以便日后 grayscale 进来仍正确)。
        // Format_RGBA8888 对齐我们的 R,G,B,A 内存序;RGB32 在小端是 BGRA = R/B 对调。
        const QImage img(reinterpret_cast<const uchar*>(m_currentFrame.data),
                         m_currentFrame.width, m_currentFrame.height,
                         m_currentFrame.stride, QImage::Format_RGBA8888);

        // Uploads img to a GPU texture within THIS render frame; the source bytes must
        // stay valid until the upload completes — guaranteed single-threaded here (the
        // pump timer cannot fire while the GUI thread is blocked in the sync phase).
        // 本渲染帧内把 img 上传 GPU;喂入字节须活到上传完——此处单线程有保证
        //(同步阶段 GUI 线程阻塞,泵定时器不会触发)。
        QSGTexture* tex = window()->createTextureFromImage(img);
        node->setTexture(tex);  // old texture auto-deleted by node (setOwnsTexture) / 旧纹理由 node 自动删(setOwnsTexture)
        m_textureDirty = false;
    }

    // Stretch the texture to the item's geometry. Cheap; runs on resize/expose too.
    // 把纹理拉伸到 item 几何。开销小;resize/expose 触发的 paint 也只走这一步。
    node->setRect(boundingRect());
    return node;
}

} // namespace indusscope::ui
