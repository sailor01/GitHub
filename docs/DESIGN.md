# FLIR Bayer 命中帧存储程序设计文档（Windows/MSVC 2022）

## 1. 目标与范围

本文档描述当前程序的总体架构、线程模型、数据流、模块职责、接口说明与调试测试建议。

适用场景：

- FLIR 高速相机（Bayer 格式）
- 170 fps 图像采集
- blob 检测后仅保存命中帧
- Windows 平台，MSVC 2022 开发

---

## 2. 系统总体架构

程序采用三段流水线并行模型：

1. **采集线程（Capture）**：负责从文件夹（临时模式）或模拟源读取帧并推入采集队列
2. **检测线程（Detect）**：从采集队列取帧，执行 blob 判定，命中后推入命中队列
3. **存储线程（Store）**：从命中队列取帧，写入 raw 文件与 `hits.csv`

### 2.1 数据流

`Frame`（采集） -> `RingQueue<shared_ptr<Frame>>` -> 检测 -> `RingQueue<pair<shared_ptr<Frame>, BlobResult>>` -> 存储

### 2.2 关键设计原则

- **采集线程轻量化**：不做重检测和磁盘写入，避免堵塞导致丢帧
- **检测与存储解耦**：通过队列隔离处理波动
- **命中帧才落盘**：降低磁盘带宽与容量压力
- **配置集中化**：所有关键参数放入 `PipelineConfig`

---

## 3. 目录与模块说明

- `src/main.cpp`：程序入口，组装默认配置并启动 Pipeline
- `src/pipeline.cpp`：核心流水线实现（采集/检测/存储线程）
- `include/app/config.hpp`：配置模型定义
- `include/app/frame.hpp`：帧和检测结果数据模型
- `include/app/ring_queue.hpp`：线程安全队列
- `include/app/pipeline.hpp`：Pipeline 外部接口

---

## 4. 类与接口说明

## 4.1 `app::PipelineConfig` 及子配置（`include/app/config.hpp`）

### `struct CameraConfig`

| 字段 | 类型 | 含义 |
|---|---|---|
| `width` | `uint32_t` | 图像宽度 |
| `height` | `uint32_t` | 图像高度 |
| `fps` | `uint32_t` | 目标采集帧率 |
| `pixel_format` | `std::string` | 像素格式（默认 `BayerRG8`） |

### `struct DetectionConfig`

| 字段 | 类型 | 含义 |
|---|---|---|
| `min_blob_area` | `uint32_t` | 最小 blob 面积阈值 |
| `max_blob_area` | `uint32_t` | 最大 blob 面积阈值 |
| `binary_threshold` | `uint32_t` | 二值化阈值（预留） |
| `min_consecutive_hits` | `uint32_t` | 连续命中帧数阈值 |

### `struct StorageConfig`

| 字段 | 类型 | 含义 |
|---|---|---|
| `output_dir` | `std::string` | 输出目录 |
| `store_color` | `bool` | 是否存彩色（当前脚手架仍保存 raw） |
| `jpeg_quality` | `uint32_t` | JPEG 质量（预留） |
| `pre_trigger_frames` | `uint32_t` | 预触发缓存帧数（预留） |
| `post_trigger_frames` | `uint32_t` | 后触发缓存帧数（预留） |

### `struct QueueConfig`

| 字段 | 类型 | 含义 |
|---|---|---|
| `capture_queue_size` | `uint32_t` | 采集队列容量 |
| `hit_queue_size` | `uint32_t` | 命中队列容量 |

### `struct PipelineConfig`

聚合配置：

- `camera` / `detection` / `storage` / `queue`
- `run_for`：运行时长（测试模式）

---

## 4.2 数据结构（`include/app/frame.hpp`）

### `struct Frame`

| 字段 | 类型 | 含义 |
|---|---|---|
| `frame_id` | `uint64_t` | 帧序号 |
| `timestamp` | `steady_clock::time_point` | 时间戳 |
| `width` | `uint32_t` | 宽 |
| `height` | `uint32_t` | 高 |
| `bayer` | `std::vector<uint8_t>` | Bayer 原始像素 |

### `struct BlobResult`

| 字段 | 类型 | 含义 |
|---|---|---|
| `frame_id` | `uint64_t` | 对应帧序号 |
| `hit` | `bool` | 是否命中 |
| `blob_count` | `uint32_t` | blob 数量 |
| `max_blob_area` | `uint32_t` | 最大 blob 面积 |

---

## 4.3 队列模板（`include/app/ring_queue.hpp`）

### `template <typename T> class RingQueue`

线程安全、带容量限制、阻塞式弹出队列。

#### 构造

- `explicit RingQueue(std::size_t capacity)`
  - 指定队列容量上限。

#### 接口

- `bool push(T value)`
  - 若队列未满且未关闭，入队并返回 `true`
  - 队列满或已关闭返回 `false`

- `std::optional<T> pop()`
  - 阻塞等待，直到有数据或队列关闭
  - 若关闭且无数据，返回 `std::nullopt`

- `void close()`
  - 标记队列关闭并唤醒全部等待者

#### 说明

- 当前实现基于 `mutex + condition_variable`，接口稳定，后续可替换为 lock-free 结构而不影响上层调用。

---

## 4.4 流水线主类（`include/app/pipeline.hpp`, `src/pipeline.cpp`）

### `class Pipeline`

#### 公有接口

- `explicit Pipeline(PipelineConfig config)`
  - 传入完整配置，保存到成员 `config_`

- `void run()`
  - 启动采集/检测/存储线程
  - 运行 `config_.run_for` 时长后停止
  - 汇总输出丢帧统计与输出目录

#### `run()` 内部流程

1. 创建两条队列：`capture_queue`、`hit_queue`
2. 启动 `capture_thread`
   - 按 `fps` 周期产生 `Frame`
   - 写入 `capture_queue`
3. 启动 `detection_thread`
   - 从 `capture_queue` 取帧
   - 调用 `detect_blob()` 得到 `BlobResult`
   - 连续命中达到阈值后推入 `hit_queue`
4. 启动 `storage_thread`
   - 从 `hit_queue` 取命中数据
   - 调用 `store_hit_frame()` 写 raw + CSV
5. 主线程 sleep 到超时，通知停止，等待三个线程退出

---

## 5. 关键内部函数说明（`src/pipeline.cpp`）

### `BlobResult detect_blob(const Frame&, const DetectionConfig&)`

- 当前是占位检测逻辑，用伪面积模拟命中。
- 后续应替换为 OpenCV 实际流程：
  1. 去噪（可选）
  2. 背景差分/帧差
  3. 二值化 + 形态学
  4. 连通域统计
  5. 面积和形状过滤

### `void store_hit_frame(const Frame&, const BlobResult&, const StorageConfig&, RuntimeStats&)`

- 写入命中帧 raw（`.raw`）
- 追加写入 `hits.csv`
- 首次写 CSV 会输出表头

---

## 6. 线程与同步策略

- 共享状态：`stop`、`dropped_frames`
- 队列关闭顺序：
  - 采集线程结束时关闭 `capture_queue`
  - 检测线程结束时关闭 `hit_queue`
- 存储线程在 `hit_queue.pop()==nullopt` 时退出

该顺序可避免消费者永久阻塞。

---

## 7. 错误与边界行为

- `push()` 失败（队列满/关闭）计入 `dropped_frames`
- 运行时长结束后统一停止
- 当前脚手架未包含异常恢复与设备断连重连机制（建议后续增强）

---

## 8. 后续扩展建议（面向真实 FLIR + OpenCV）

1. **Spinnaker 相机后端**
   - 在 `capture_thread` 替换模拟帧生成为 `GetNextImage()`
   - 增加掉线重连与超时策略

2. **Bayer 检测路径优化**
   - 检测阶段优先单通道处理
   - 命中帧再做去马赛克并保存彩色

3. **时延与吞吐指标**
   - 增加每阶段耗时统计（capture/detect/store）
   - 定期输出队列占用率和瞬时 fps

4. **预触发/后触发缓存**
   - 使用环形缓存实现事件上下文帧保存

5. **配置文件化**
   - 从 JSON/TOML 载入 `PipelineConfig`，便于现场调参

---

## 9. 调试与测试计划（建议）

### 第 1 阶段：链路打通

- 用模拟源跑满 170fps
- 验证：无崩溃、线程正常退出、CSV 正常生成

### 第 2 阶段：检测替换

- 接入 OpenCV blob 检测
- 验证：命中逻辑稳定、误报漏报可接受

### 第 3 阶段：真实相机接入

- 接入 Spinnaker
- 验证：长稳运行（>30min）掉帧率、延迟与磁盘占用

### 第 4 阶段：参数收敛

- 根据现场光照/速度调参
- 固化阈值与运行配置

---

## 10. 对外接口最小清单（供二次开发）

- `app::PipelineConfig`：统一配置入口
- `app::Pipeline::Pipeline(PipelineConfig)`：实例化运行器
- `app::Pipeline::run()`：执行整个采集-检测-存储流程
- `app::RingQueue<T>`：可复用于其他并发模块
- `app::Frame` / `app::BlobResult`：跨线程数据契约



## 11. 新增能力说明（本次更新）

### 11.1 文件夹图像输入（临时代替相机）

新增 `InputMode` 与 `InputConfig`：

- `InputMode::Folder`：从 `image_folder` 读取测试图像
- 支持 `.raw`、`.pgm(P5)`
- `loop_folder=true` 时循环回放；否则读取完后停止

读取行为：

- `.raw`：按 `camera.width * camera.height` 读入
- `.pgm`：从文件头解析宽高

### 11.2 blob 处理时间统计

在检测线程中对每帧 `detect_blob()` 统计：

- 总耗时（微秒）
- 最大耗时（微秒）
- 平均耗时（微秒）

最终输出：

- `detect_time_avg_us`
- `detect_time_max_us`

### 11.3 命中帧存储时间统计

在存储线程 `store_hit_frame()` 中统计：

- 总耗时（微秒）
- 最大耗时（微秒）
- 平均耗时（微秒）

最终输出：

- `store_time_avg_us`
- `store_time_max_us`

### 11.4 统计字段总览

运行结束时统一输出：

- `processed_frames`
- `stored_frames`
- `dropped_frames`
- `detect_time_avg_us` / `detect_time_max_us`
- `store_time_avg_us` / `store_time_max_us`
