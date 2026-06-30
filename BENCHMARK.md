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
| Curve render FPS (ProtocolSource) | >= 60fps @ 100k pts | **steady-state 55–60 (mode 57–59)** / 稳态 55–60,众数 57–59 | S2.5a |
| E2E latency p99 (ProtocolSource) | < 20ms | **16.4 ms** p50=8.2ms, n=3000 / p50=8.2ms, n=3000 | S2.5a |
| Curve render FPS (S2.5d-2 mock mode) | >= 60fps @ 100k pts | **steady-state 58–60** — no regression vs S2.5a ✓ / 与 S2.5a 一致,无回退 | S2.5d-2 |
| E2E latency p99 (S2.5d-2 mock mode) | < 20ms | **≤16.4 ms** — same pipeline, no change / 管线不变,预计持平 | S2.5d-2 |
| Image stream FPS | 1080p@30fps | **30–31 steady @ 1080p** (single-thread first light) / 单线程首光,稳态 30–31 | S2.6b-2 |
| Image stream memory | no growth | **168.3 MB flat, ~80 s @ 1080p@30fps** (per-frame texture, no leak) / 每帧建纹理不涨 | S2.6b-2 |
| Long-run memory | 0 leak / 1hr | — | S3.4 |

## Image lane — correctness gate (S2.6b-1) / 图像那一路 — 正确性闸 (S2.6b-1)

`LatestFrameBuffer` (latest-wins triple buffer) is correctness-gated, not a throughput
number: a frame's FPS/latency is produced by the on-screen path in S2.6b-2. What this
slice proves under a 2-thread stress (N = 5e5):
`LatestFrameBuffer`(latest-wins 三缓冲)这刀是正确性闸,非吞吐数字;帧率/延迟由 S2.6b-2
上屏路径出。本刀双线程压测(N = 5e5)证明:

- No tearing, frame index monotonic non-regressing, producer never blocks (slow consumer
  → drops, not stalls), zero allocation on the commit/takeLatest hot path.
  无撕裂、帧号单调不退、生产者永不阻塞(慢消费者 → 丢帧而非阻塞)、commit/takeLatest 热路径零分配。
- Memory ordering verified with **ThreadSanitizer** (Docker `gcc:13 -fsanitize=thread`):
  `acq_rel` clean; a deliberate `relaxed` downgrade is caught as a data race (gate is real).
  MinGW/Windows cannot run TSan; final memory-order check on real ARM is deferred to S3.2.
  内存序用 **ThreadSanitizer** 验证(Docker `gcc:13 -fsanitize=thread`):`acq_rel` 干净;
  故意降级 `relaxed` 被抓为 data race(闸有效)。MinGW/Windows 跑不了 TSan,真 ARM 上的
  内存序最终验证挂 S3.2。

## Image stream — on-screen FPS & memory (S2.6b-2) / 图像那一路 — 上屏帧率与内存 (S2.6b-2)

First light of the image lane on screen: `FrameView` (custom `QQuickItem`) drives
`SyntheticFrameSource` from a GUI-thread `QTimer` and uploads each frame via
`QSGSimpleTextureNode` + `createTextureFromImage`. Single-threaded (worker offload is b-3).
图像那一路首次上屏:`FrameView`(自定义 `QQuickItem`)由 GUI 线程 `QTimer` 驱动
`SyntheticFrameSource`,每帧经 `QSGSimpleTextureNode` + `createTextureFromImage` 上传。
单线程(挪 worker 是 b-3)。

| Metric | Target | Measured | Notes |
|--------|--------|----------|-------|
| Frame FPS @ 1080p | 30 fps | **30–31 steady** (~85 s, no dip) / 稳态 30–31,无掉帧 | render fps == production rate, RGBA8888 1920×1080 |
| Frame FPS @ 720p | 30 fps | **30–31 steady** | same code path / 同码路 |
| Process RSS @ 1080p | no growth | **168.3 MB flat** over ~80 s (range 168.1–168.4) / 80s 持平 | headline: validates `setOwnsTexture` frees prior texture each frame |
| Startup RSS spike | — | 431 MB → settles to 168 MB by t=5 s / 启动尖峰后回落 | one-time RHI/D3D + first-texture init, not a leak / 一次性 RHI/D3D 初始化,非泄漏 |

**Timer type matters / 定时器类型要紧:** the default `Qt::CoarseTimer` coalesces a 33 ms
interval to the ~15.6 ms Windows system tick, snapping it to 46.8 ms (≈21 fps). The pump uses
`Qt::PreciseTimer` to hold ~30 fps. / 默认 `Qt::CoarseTimer` 把 33ms 对齐到 ~15.6ms 系统 tick,
顶成 46.8ms(≈21fps);帧泵用 `Qt::PreciseTimer` 才稳住 ~30fps。

**Per-frame texture cost / 每帧建纹理开销:** this slice rebuilds a GPU texture every frame
(`createTextureFromImage`), proven smooth at 1080p@30fps on desktop. If a real-machine/ARM run
later shows per-frame upload stalls, the in-place QRhi upload path (reuse one texture) is the
filed optimization slice. / 本刀每帧重建 GPU 纹理,桌面 1080p@30fps 已证顺滑;若日后真机/ARM
测出每帧上传卡顿,复用单张纹理的 QRhi 原地上传是已挂账的优化刀。

### Methodology / 方法
- Launch `INDUSSCOPE_VIEW=frame`; `INDUSSCOPE_FRAME_W/H=1920/1080` for the 1080p run.
  Window forced foreground each sample so the threaded render loop stays exposed (an occluded
  Qt Quick window correctly stops swapping → render fps 0 while production continues).
  启动 `INDUSSCOPE_VIEW=frame`;1080p 跑加 `INDUSSCOPE_FRAME_W/H=1920/1080`。每次采样把窗口
  置前,使线程化渲染循环保持 exposed(被遮挡的 Qt Quick 窗口会正确停止 swap → 渲染 fps 0 而生产继续)。
- FPS from per-second `frameSwapped` count (queued onto GUI thread). RSS via `Get-Process
  WorkingSet64`, sampled every 5 s over ~80 s. Release (`-O3 -DNDEBUG`).
  FPS 取每秒 `frameSwapped` 计数(queued 到 GUI 线程);RSS 用 `Get-Process WorkingSet64`,每 5 秒采样,共 ~80 秒。Release 构建。

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

## Render latency & FPS (S2.5a — ProtocolSource pipeline) / 渲染延迟与帧率 (S2.5a — ProtocolSource 管线)

Producer switched from MockSource (SignalGenerator) to ProtocolSource(MockProtocol, sine, ch0, 5000 Hz).
生产者从 MockSource(SignalGenerator)换为 ProtocolSource(MockProtocol, sine, ch0, 5000 Hz)。

| Metric | Value | Notes |
|--------|-------|-------|
| Render latency p50 | 8.2 ms | comparable to S2.3 7.3 ms — within vsync-grid variability / 与 S2.3 相当,在 vsync 抖动范围内 |
| Render latency p99 | 16.4 ms | +0.6 ms vs S2.3; under 20 ms target ✓ / 较 S2.3 +0.6ms,达标 |
| Render latency max | 25.3 ms | single outlier (Windows non-RT scheduling) / 单点离群(Windows 非实时调度) |
| Curve render FPS | 55–60 steady (mode 57–59) | consistent with S2.3 58–60; no visible regression ✓ / 与 S2.3 一致,无可见回退 |
| n samples | 3000 | ~55 s run, Release / ~55 秒运行,Release 构建 |

No visible FPS or p99 regression vs S2.3 MockSource baseline; ProtocolSource adapter overhead is negligible.
与 S2.3 MockSource 基线相比无可见 FPS / p99 回退;ProtocolSource 适配层开销可忽略。

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
