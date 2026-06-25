// Standalone ThreadSanitizer harness for LatestFrameBuffer — NO Catch2, so it
// builds under WSL/Linux clang/gcc with -fsanitize=thread without extra deps.
// This is the memory-order gate the Catch2 ctest (MinGW x86) cannot be:
// release/acquire vs relaxed is an x86-green / ARM-red trap; only TSan (which
// models the C++ memory model, not x86) catches a relaxed downgrade as a race.
//
// LatestFrameBuffer 的独立 ThreadSanitizer 压测——无 Catch2,可在 WSL/Linux 的
// clang/gcc 下用 -fsanitize=thread 直接编译,免额外依赖。这是 MinGW x86 的 ctest
// 当不了的内存序闸:release/acquire vs relaxed 是 x86 绿 / ARM 红的活靶子,
// 唯有 TSan(建模 C++ 内存模型而非 x86)才能把 relaxed 降级当作 race 抓出。
//
// Build & run (from repo root, in WSL) / 在 WSL 仓库根目录编译运行:
//   g++ -std=c++17 -O1 -g -fsanitize=thread -pthread -Icore/include \
//       core/src/FramePool.cpp core/src/LatestFrameBuffer.cpp \
//       core/tests/tsan/latest_frame_buffer_tsan_main.cpp -o /tmp/lfb_tsan && /tmp/lfb_tsan
//
// Exit code 0 + "OK" and a clean TSan report = pass.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <thread>

#include "indusscope/core/ImageFrame.h"
#include "indusscope/core/LatestFrameBuffer.h"

using namespace indusscope::core;

namespace {

void fillSentinel(const ImageFrame& f, std::uint64_t seq) {
    std::byte* base = f.data;
    const std::size_t total = static_cast<std::size_t>(f.stride) * static_cast<std::size_t>(f.height);
    const std::byte v = static_cast<std::byte>(seq & 0xFF);
    for (std::size_t i = 8; i < total; ++i)
        base[i] = v;
    std::uint64_t s = seq;
    for (int b = 0; b < 8; ++b) {
        base[b] = static_cast<std::byte>(s & 0xFF);
        s >>= 8;
    }
}

// Returns seq if internally consistent, 0 if torn (producer seq starts at 1).
// 整帧一致返回 seq,撕裂返回 0(生产者 seq 从 1 起)。
std::uint64_t checkSentinel(const ImageFrame& f) {
    const std::byte* base = f.data;
    std::uint64_t seq = 0;
    for (int b = 7; b >= 0; --b)
        seq = (seq << 8) | static_cast<std::uint8_t>(base[b]);
    const std::byte v = static_cast<std::byte>(seq & 0xFF);
    const std::size_t total = static_cast<std::size_t>(f.stride) * static_cast<std::size_t>(f.height);
    const std::size_t pts[] = {8, total / 4, total / 2, (total * 3) / 4, total - 1};
    for (std::size_t p : pts) {
        if (p >= 8 && base[p] != v)
            return 0;
    }
    return seq;
}

} // namespace

int main() {
    constexpr std::int32_t W = 64, H = 48;
    constexpr std::uint64_t N = 500000;
    LatestFrameBuffer buf(W, H);

    std::atomic<bool>          producer_done{false};
    std::atomic<std::uint64_t> torn{0};
    std::atomic<std::uint64_t> regressed{0};
    std::atomic<std::uint64_t> consumed{0};
    std::uint64_t              final_seq = 0;

    std::thread producer([&] {
        for (std::uint64_t seq = 1; seq <= N; ++seq) {
            fillSentinel(buf.writeSlot(), seq);
            buf.commit(static_cast<std::int64_t>(seq));
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::uint64_t last = 0;
        for (;;) {
            std::optional<ImageFrame> f = buf.takeLatest();
            if (!f) {
                if (producer_done.load(std::memory_order_acquire)) {
                    f = buf.takeLatest();
                    if (!f)
                        break;
                } else {
                    continue;
                }
            }
            const std::uint64_t seq = checkSentinel(*f);
            if (seq == 0) {
                torn.fetch_add(1, std::memory_order_relaxed);
            } else {
                if (seq < last)
                    regressed.fetch_add(1, std::memory_order_relaxed);
                last = seq;
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        }
        final_seq = last;
    });

    producer.join();
    consumer.join();

    const std::uint64_t t = torn.load();
    const std::uint64_t r = regressed.load();
    const std::uint64_t c = consumed.load();
    std::printf("torn=%llu regressed=%llu consumed=%llu final_seq=%llu dropped=%llu\n",
                static_cast<unsigned long long>(t), static_cast<unsigned long long>(r),
                static_cast<unsigned long long>(c), static_cast<unsigned long long>(final_seq),
                static_cast<unsigned long long>(buf.dropped()));

    if (t != 0 || r != 0 || c == 0 || final_seq != N) {
        std::printf("FAIL\n");
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
