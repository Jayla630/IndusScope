# IndusScope — Performance Benchmarks

> Performance data recorded from Slice 2.2 (lock-free SPSC queue).
> 性能数据自 Slice 2.2 (无锁 SPSC 队列) 开始记录。

## Hardware / 硬件

| Item | Value |
|------|-------|
| CPU | Intel Core (Windows 11, MinGW GCC 13.1.0) |
| Compiler | g++ (MinGW-w64) 13.1.0, -O3 -DNDEBUG |
| OS | Windows 11 Home China 10.0.26200 |

## RingBuffer SPSC Throughput / 环形缓冲 SPSC 吞吐

| Metric | Target | Measured | Slice | Notes |
|--------|--------|----------|-------|-------|
| Single-thread push | — | **228 M ops/s** | S2.2 | 5M `uint64_t` into 65536-cap buffer; pure write path |
| Cross-thread SPSC 0-drop | — | **~77 M ops/s** | S2.2 | 10M items, cap 64, 2 threads (producer + consumer w/ pop_batch) |
| Cross-thread SPSC forced-drop | — | **~67 M ops/s** | S2.2 | 10M items, cap 256, consumer throttled every 4096 pops |

**Headroom vs 1 kHz sampling target / 对比 1kHz 采样目标的余量:**

- Single-thread headroom: 228,000,000 / 1,000 ≈ **228,000×**
- Cross-thread headroom: 77,000,000 / 1,000 ≈ **77,000×**

Both are several orders of magnitude beyond the 1 kHz requirement. The bottleneck
for the full pipeline will be elsewhere (serial I/O, rendering, signal processing).
两端吞吐均超出 1kHz 采样目标多个数量级。全链瓶颈将在别处 (串口 I/O、渲染、信号处理)。

## Metrics

| Metric | Target | Measured | Slice |
|--------|--------|----------|-------|
| Sampling throughput | >= 1kHz | — | S2.2 |
| Curve render FPS | >= 60fps @ 100k pts | — | S2.3 |
| E2E latency p99 | < 20ms | — | S2.3 |
| Image stream | 1080p@30fps | — | S2.6 |
| Long-run memory | 0 leak / 1hr | — | S3.4 |

## Methodology

- Single-thread: timing via `std::chrono::steady_clock`, 5M sequential pushes, 65536-cap buffer.
- Cross-thread: timing via ctest wall-clock (includes thread create/join). Consumer uses
  `pop_batch(256)` for the 0-drop run, single `pop()` for the forced-drop run.
  FIFO integrity verified by strict monotonicity check on received values.
- All measurements on Release build (`-O3 -DNDEBUG`).
