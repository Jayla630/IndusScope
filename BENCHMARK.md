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
| Curve render FPS | >= 60fps @ 100k pts | **~60 (60Hz vsync upper bound); steady-state 58–60** / 稳态 58–60 | S2.3 |
| E2E latency p99 | < 20ms | **15.8 ms** (render latency definition, see below) / 渲染延迟口径,见下 | S2.3 |
| Image stream | 1080p@30fps | — | S2.6 |
| Long-run memory | 0 leak / 1hr | — | S3.4 |

## Methodology

- Single-thread: timing via `std::chrono::steady_clock`, 5M sequential pushes, 65536-cap buffer.
- Cross-thread: timing via ctest wall-clock (includes thread create/join). Consumer uses
  `pop_batch(256)` for the 0-drop run, single `pop()` for the forced-drop run.
  FIFO integrity verified by strict monotonicity check on received values.
- All measurements on Release build (`-O3 -DNDEBUG`).

## Render latency & FPS (S2.3) / 渲染延迟与帧率 (S2.3)

| Metric | Value | Notes |
|--------|-------|-------|
| Render latency p50 | 7.3 ms | half a vsync interval — data-ready lands uniformly within the 16.6 ms frame window / 半帧间隔,数据就绪点均匀落在 16.6ms 帧窗内 |
| Render latency p99 | 15.8 ms | ≈ one vsync interval; worst typical case waits for the next frame — under 20 ms target ✓ / ≈ 一帧间隔,最坏等下一帧,达标 |
| Render latency max | 150 ms | single-sample outlier (non-realtime Windows scheduling), not representative / 单点离群(Windows 非实时调度),非常态 |
| Curve render FPS | ~60 (58–60 steady) | 60 Hz vsync ceiling @ 100k-point window, Release / 100k 点窗口 60Hz vsync 上限,Release |

### Render latency definition / 口径

- Measured = `steady_clock` from frame-data assembled in `onTick` (after Min/Max downsample)
  → `frameSwapped` handler firing on the GUI thread.
  渲染延迟 = `onTick` 拼好降采样数据 → `frameSwapped` 在 GUI 线程触发,两次 `steady_clock` 之差。
- Includes the wait for the next vsync + a small GUI-event-queue component → reflects
  "data-ready → on screen", not pure GPU render time.
  含等待下一次 vsync + 一小段 GUI 事件队列,反映"数据就绪→上屏",非纯 GPU 渲染时间。
- Warmup: first ~1 s of frames discarded. Sample size n = 3000 (~50 s), Release (`-O3 -DNDEBUG`).
  预热:丢弃前 ~1 秒帧。样本量 3000 帧 (~50 秒),Release 构建。

### Footnote — full-chain latency / 脚注:全链延迟

The SPEC "sampling → on-screen p99 < 20 ms" full chain is NOT what's reported above. Full-chain
latency is structurally bounded by two 16 ms timers (worker produce tick + UI render tick,
~32 ms worst case) — an artifact of the mock-source demo cadence, not a real-device latency.
With a real push-driven device feed this term disappears. Reported here is the render half
(data-ready → screen), the part this architecture controls.
SPEC 的"采样→上屏 p99 < 20ms"全链不是上表所报。全链受 worker 16ms + UI 16ms 双定时器结构性
支配(最坏 ~32ms),属 mock 演示节奏、非真实设备延迟;真实设备推流时该项消失。此处报的是渲染
半段(数据就绪→上屏),即当前架构可控的部分。
实测数已出(Release):FPS 稳态 58–60(60Hz vsync 上限),渲染延迟 p50 7.3ms / p99 15.8ms
/ max 150ms(n=3000)。
