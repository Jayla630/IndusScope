#include "indusscope/ui/AcquisitionWorker.h"

#include <QTimer>
#include <QThread>
#include <QDebug>

namespace indusscope::ui {

AcquisitionWorker::AcquisitionWorker(
    indusscope::core::RingBuffer<indusscope::core::SamplePoint>& sink,
    const indusscope::core::MockSourceConfig& cfg,
    QObject* parent)
    : QObject(parent)
    , m_source(cfg, sink)                           // construct MockSource in-place with sink ref
                                                    // 用 sink 引用就地构造 MockSource
    , m_produceTimer(new QTimer(this))              // parent=this → Qt parent-child ownership, RAII
                                                    // parent=this → Qt 父子所有权,RAII
{
    m_produceTimer->setInterval(kTimerIntervalMs);
    connect(m_produceTimer, &QTimer::timeout, this, &AcquisitionWorker::onProduceTick);
}

void AcquisitionWorker::start()
{
    if (m_running)
        return;
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
    m_source.produce(kProducePerTick);
}

} // namespace indusscope::ui
