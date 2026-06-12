#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>
#include "indusscope/protocol/ITransport.h"

namespace indusscope::protocol {

/// Test-only scripted transport: replays pre-recorded response chunks, records sent bytes.
/// 仅测试用的脚本化传输:回放预录的响应分块,记录被发送的字节。
/// A single recv() never crosses a chunk boundary — enqueue small chunks to force partial reads.
/// 单次 recv() 绝不跨分块边界——入队小分块即可逼出部分读取路径。
/// Lives in protocol/tests/, never ships in the library.
/// 仅存在于 protocol/tests/,不进 shipping 库。
class FakeTransport final : public ITransport {
public:
    bool open() override { m_open = true; return true; }
    void close() override { m_open = false; }
    bool isOpen() const override { return m_open; }

    bool send(const std::uint8_t* data, std::size_t n) override {
        if (!m_open || m_fail_send) return false;
        m_sent.insert(m_sent.end(), data, data + n);
        return true;
    }

    int recv(std::uint8_t* out, std::size_t max_n) override {
        if (!m_open) return -1;
        if (m_chunks.empty()) return 0; // scripted bytes exhausted = timeout / 预录字节耗尽视为超时
        auto& chunk = m_chunks.front();
        const std::size_t take = std::min(max_n, chunk.size() - m_pos);
        std::copy_n(chunk.data() + m_pos, take, out);
        m_pos += take;
        if (m_pos == chunk.size()) {
            m_chunks.pop_front();
            m_pos = 0;
        }
        return static_cast<int>(take);
    }

    /// Enqueue one response chunk; several small chunks simulate fragmented TCP delivery.
    /// 入队一个响应分块;多个小分块模拟 TCP 分片到达。
    void enqueueChunk(std::vector<std::uint8_t> bytes) { m_chunks.push_back(std::move(bytes)); }

    /// All bytes passed to send(), in order — for asserting request frames.
    /// send() 收到的全部字节(按序)——用于断言请求帧。
    const std::vector<std::uint8_t>& sent() const { return m_sent; }

    /// Forget recorded sent bytes (between polls in one test).
    /// 清空已记录的发送字节(同一测试内两次 poll 之间用)。
    void clearSent() { m_sent.clear(); }

    /// Force send() to fail, exercising the send error path.
    /// 强制 send() 失败,覆盖发送错误路径。
    void setFailSend(bool fail) { m_fail_send = fail; }

private:
    std::deque<std::vector<std::uint8_t>> m_chunks; // scripted recv data / 预录的接收数据
    std::size_t m_pos{0};                           // read offset in front chunk / 队首分块内的读偏移
    std::vector<std::uint8_t> m_sent;               // recorded sent bytes / 已发送字节记录
    bool m_open{false};                             // open state / 打开状态
    bool m_fail_send{false};                        // forced send failure / 强制发送失败开关
};

} // namespace indusscope::protocol
