#include "indusscope/ui/AcquisitionWorker.h"

#include <QTimer>
#include <QThread>
#include <QDebug>

namespace indusscope::ui {

AcquisitionWorker::AcquisitionWorker(
    std::unique_ptr<indusscope::protocol::IDeviceProtocol> proto,
    indusscope::core::RingBuffer<indusscope::core::SamplePoint>& sink,
    const indusscope::protocol::ProtocolConfig& cfg,
    std::uint32_t channel,
    QObject* parent)
    : QObject(parent)
    , m_source(std::move(proto), sink, cfg, channel)  // protocol ownership transferred; sink ref held
                                                       // 协议所有权移入; sink 引用持有
    , m_produceTimer(new QTimer(this))                 // parent=this → Qt parent-child ownership, RAII
                                                       // parent=this → Qt 父子所有权,RAII
{
    m_produceTimer->setInterval(kTimerIntervalMs);
    connect(m_produceTimer, &QTimer::timeout, this, &AcquisitionWorker::onProduceTick);
}

void AcquisitionWorker::start()
{
    if (m_running)
        return;

    // configure() + open() run here — in worker thread via QueuedConnection.
    // configure() + open() 在此运行——通过 QueuedConnection 在 worker 线程执行。
    if (!m_source.start()) {
        emit error(QStringLiteral("[Worker] protocol open() failed — check ProtocolConfig"));
        return; // bail: no timer, no m_running flip / 提前返回:不启动定时器,不翻转 m_running
    }

    m_running = true;
    qDebug() << "[Worker] start on thread" << QThread::currentThreadId();
    m_produceTimer->start();
    emit started();
}

void AcquisitionWorker::stop()
{
    if (!m_running)
        return;
    qDebug() << "[Worker] stop on thread" << QThread::currentThreadId();
    m_produceTimer->stop();
    m_source.stop(); // close() in worker thread / close() 在 worker 线程执行
    m_running = false;
    emit stopped();
}

void AcquisitionWorker::onProduceTick()
{
    // Log thread id once to prove production has left UI thread.
    // 打印一次线程 id,证明生产已离开 UI 线程。
    if (!m_loggedThread) {
        m_loggedThread = true;
        qDebug() << "[Worker] onProduceTick on thread" << QThread::currentThreadId();
    }
    m_source.acquire(kProducePerTick);
}

} // namespace indusscope::ui
