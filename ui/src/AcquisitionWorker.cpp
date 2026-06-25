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

    m_failCount = 0;
    m_offline   = false;

    // configure() + open() run here — in worker thread via QueuedConnection.
    // configure() + open() 在此运行——通过 QueuedConnection 在 worker 线程执行。
    if (!m_source.start()) {
        // Device unreachable — enter offline backoff immediately; slow timer will retry.
        // 设备不可达——立即进入离线退避;慢 timer 定期重连。
        m_offline = true;
        m_running = true;
        m_produceTimer->setInterval(kReconnectIntervalMs);
        m_produceTimer->start();
        emit error(QStringLiteral("[Worker] protocol open() failed — device offline"));
        return;
    }

    m_running = true;
    qDebug() << "[Worker] start on thread" << QThread::currentThreadId();
    m_produceTimer->setInterval(kTimerIntervalMs); // fast produce rate / 快速采集节奏
    m_produceTimer->start();
    emit started();
}

void AcquisitionWorker::stop()
{
    if (!m_running)
        return;
    qDebug() << "[Worker] stop on thread" << QThread::currentThreadId();
    m_produceTimer->stop();
    m_produceTimer->setInterval(kTimerIntervalMs); // reset for next start() / 为下次 start() 重置
    m_source.stop();
    m_running   = false;
    m_offline   = false;
    m_failCount = 0;
    emit stopped();
}

void AcquisitionWorker::onProduceTick()
{
    if (m_offline) {
        // --- Slow reconnect path 慢速重连路径 ---
        // close any half-open socket before retrying / 重试前关闭任何半开 socket
        m_source.stop();
        if (m_source.start()) {
            // Device came back — resume fast production. / 设备恢复——切回快速采集。
            m_offline   = false;
            m_failCount = 0;
            m_produceTimer->setInterval(kTimerIntervalMs);
            emit error(QString{});  // clear error in CurveController / 清除 CurveController 中的错误
            qDebug() << "[Worker] reconnected — resuming fast production";
        }
        // On failure: stay offline; error already emitted on offline entry; no re-emit.
        // 失败:保持离线;进入离线时已发射 error,不重复发射。
        return;
    }

    // --- Normal fast produce path 正常快速采集路径 ---
    if (!m_loggedThread) {
        m_loggedThread = true;
        qDebug() << "[Worker] onProduceTick on thread" << QThread::currentThreadId();
    }

    const std::size_t n = m_source.acquire(kProducePerTick);
    if (n == 0) {
        // Nothing produced this tick — count consecutive failures.
        // 本 tick 无产出——计连续失败次数。
        if (++m_failCount >= kMaxConsecutiveFails) {
            // Enter offline backoff: switch to slow reconnect timer.
            // 进入离线退避:切到慢速重连 timer。
            m_offline   = true;
            m_failCount = 0;
            m_produceTimer->setInterval(kReconnectIntervalMs);
            emit error(QStringLiteral("[Worker] device offline — reconnecting..."));
        }
    } else {
        m_failCount = 0; // any success resets the failure streak / 任何成功均清零失败计数
    }
}

} // namespace indusscope::ui
