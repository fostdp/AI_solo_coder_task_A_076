# 水轮机空化噪声监测与寿命评估系统

大型水电站6×混流式水轮机空化在线监测、叶片疲劳寿命评估与智能告警系统。

## 系统架构

```
                        ┌──────────────────────────────────────────────────────┐
                        │                  Nginx (端口 3000)                    │
                        │         Gzip压缩 · 反向代理 · 静态资源               │
                        └──────────┬──────────────────────────┬───────────────┘
                                   │ /api/*                  │ /*
                                   ▼                         ▼
┌──────────────┐  UDP:9000  ┌─────────────────────────────────────────┐
│ PXI 模拟器   │───────────▶│       C++ 空化监测服务 (端口 8080)       │
│ (6台×20传感器│            │                                         │
│  1ms间隔)    │            │  ┌─────────────┐   MPSC Queue           │
└──────────────┘            │  │ pxi_collector│──────┐                 │
                            │  └─────────────┘      ▼                 │
                            │  ┌──────────────────────────┐           │
                            │  │ cavitation_detector       │           │
                            │  │ (孤立森林+深度自编码器)    │           │
                            │  └──────────────────────────┘           │
                            │              │ MPSC Queue               │
                            │              ▼                          │
                            │  ┌──────────────────────────┐           │
                            │  │ fatigue_evaluator         │           │
                            │  │ (流式雨流计数+Miner损伤)   │           │
                            │  └──────────────────────────┘           │
                            │              │ MPSC Queue               │
                            │              ▼                          │
                            │  ┌──────────────────────────┐           │
                            │  │ alarm_pusher              │           │
                            │  │ (告警+IEC 61850+ClickHouse)│           │
                            │  └──────────────────────────┘           │
                            │              │                           │
                            │         ┌────┴────┐                     │
                            │         │Prometheus│ :9090/metrics      │
                            │         │ spdlog   │ logs/*.log        │
                            │         └─────────┘                     │
                            └─────────────────────────────────────────┘
                                   │                    │
                    ┌──────────────▼──┐       ┌─────────▼──────────┐
                    │   ClickHouse    │       │ IEC 61850 模拟器    │
                    │  (端口 8123/9000)│       │  (端口 102/8092)   │
                    │ TTL+自动归档    │       │  MMS报告接收验证    │
                    └─────────────────┘       └────────────────────┘
```

## 模块说明

### C++ 后端服务 (模块化管线)

| 模块 | 文件 | 职责 |
|------|------|------|
| `pxi_collector` | `backend/include/pxi_collector.h` + `.cpp` | 高速UDP接收(recvmmsg/IOCP) + FFT频谱特征计算 |
| `cavitation_detector` | `backend/include/cavitation_service.h` + `.cpp` | 孤立森林+深度自编码器+工况归一化+自适应阈值空化识别 |
| `fatigue_evaluator` | `backend/include/fatigue_service.h` + `.cpp` | 流式四点雨流计数 + Miner累积损伤 + S-N曲线寿命计算 |
| `alarm_pusher` | `backend/include/alarm_service.h` + `.cpp` | ClickHouse写入 + 告警判定 + IEC 61850 MMS推送 |

模块间通过 **Dmitry Vyukov MPSC无锁队列** 通信 (Capacity=8192, alignas(64))。

### 前端模块

| 模块 | 文件 | 职责 |
|------|------|------|
| `Turbine3DViewer` | `frontend/src/turbine_3d_viewer.js` | Canvas 2D水轮机剖面图 + 空化云图 + 点击交互 |
| `BladeDetailPanel` | `frontend/src/blade_detail.js` | 叶片空化历史趋势 + 累计损伤 + 检修建议 |
| WebGL瀑布图 | `frontend/src/webglRenderer.js` | WebGL2频谱瀑布图 + WebWorker + LOD降采样 |

### Docker容器编排

| 容器 | 镜像 | 端口 |
|------|------|------|
| `cavitation-clickhouse` | clickhouse/clickhouse-server:24.1 | 8123(HTTP), 9000(Native) |
| `cavitation-monitor` | 自建C++镜像 | 8080(API), 9000/udp(PXI), 9090(Prometheus) |
| `pxi-simulator` | 自建Python镜像 | 8091(注入控制) |
| `iec61850-simulator` | 自建Python镜像 | 102(MMS), 8092(状态查询) |
| `cavitation-frontend` | nginx:1.25-alpine | 3000(HTTP+Gzip) |

## 快速部署

### 前置条件

- Docker 24.0+ & Docker Compose v2.20+
- 可选: CMake 3.16+ & GCC 11+ (本地编译C++后端)

### Docker Compose 一键启动

```bash
cd docker
docker-compose up -d

# 等待服务就绪 (~30s)
docker-compose logs -f cavitation-monitor
```

### 验证服务状态

```bash
# 前端界面
open http://localhost:3000

# API健康检查
curl http://localhost:3000/health

# ClickHouse查询
curl 'http://localhost:8123/?query=SELECT+count()+FROM+cavitation_monitor.alarm_record'

# Prometheus指标
curl http://localhost:9090/metrics

# IEC 61850模拟器状态
curl http://localhost:8092/stats
```

### 停止服务

```bash
cd docker
docker-compose down -v   # -v 清除数据卷
```

## PXI模拟器用法

### 基本启动

模拟器默认配置: 6台水轮机 × 20传感器 × 1ms间隔

```bash
# Docker内自动启动，或本地运行:
python scripts/pxi_simulator.py \
  --target-ip 127.0.0.1 \
  --target-port 9000 \
  --turbines 6 \
  --sample-rate 1000 \
  --interval-ms 1 \
  --inject-port 8091
```

### 注入空化特征信号

模拟器支持HTTP API动态注入不同阶段的空化特征:

```bash
# 在3号机组7号叶片注入临界空化
curl -X POST http://localhost:8091/inject \
  -H 'Content-Type: application/json' \
  -d '{"turbine_id":3,"blade_id":7,"stage":"critical","intensity":0.8}'

# 在1号机组所有叶片注入发展空化
curl -X POST http://localhost:8091/inject \
  -H 'Content-Type: application/json' \
  -d '{"turbine_id":1,"stage":"developed","intensity":0.9}'

# 全部机组注入初生空化
curl -X POST http://localhost:8091/inject \
  -H 'Content-Type: application/json' \
  -d '{"stage":"incipient","intensity":0.5}'

# 清除所有注入，恢复自然演化
curl -X DELETE http://localhost:8091/inject

# 查询当前所有叶片状态
curl http://localhost:8091/status
```

**空化阶段说明:**

| 阶段 | `stage`值 | 水听器特征 | 加速度计特征 |
|------|----------|-----------|------------|
| 无空化 | `none` | 基线噪声 | 基线振动 |
| 初生空化 | `incipient` | 20-30kHz超声频带增强 | 200-500Hz微振动 |
| 临界空化 | `critical` | 超高频+随机突发脉冲 | 500-1000Hz强振动+冲击 |
| 发展空化 | `developed` | 全频段强噪声+低频调制+40-60kHz高频 | 宽带随机+5-15Hz低频调制 |

## IEC 61850模拟器

```bash
# 本地运行
python scripts/iec61850_simulator.py --listen-port 102 --http-port 8090

# 查询接收到的MMS报告
curl http://localhost:8092/reports

# 查看统计
curl http://localhost:8092/stats

# 清除报告缓存
curl -X DELETE http://localhost:8092/reports
```

## 模型参数配置

所有模型参数外置于 `backend/config/model_config.json`:

```json
{
  "isolation_forest": { "n_trees": 100, "sample_size": 256 },
  "autoencoder": { "input_dim": 19, "encoding_dim": 16, "learning_rate": 0.001 },
  "ensemble": { "if_weight": 0.6, "ae_weight": 0.4 },
  "adaptive_threshold": { "window_size": 512, "max_offset": 0.15 },
  "fatigue": { "cavitation_factor": 1.5, "design_life_hours": 100000 },
  "sn_curve": { "N": [...], "S": [...] },
  "alarm_thresholds": { "cavitation_intensity_limit": 0.6 },
  "server": { "udp_port": 9000, "api_port": 8080, "prometheus_port": 9090 },
  "logging": { "level": "info", "max_file_size_mb": 10, "max_files": 5 }
}
```

## ClickHouse数据生命周期

| 表 | 热数据TTL | 冷存储迁移 | 归档保留 |
|----|----------|-----------|---------|
| `raw_signal` | 7天 | 3天→cold | - |
| `spectrum_feature` | 90天 | 30天→cold | - |
| `cavitation_status` | 365天 | 90天→cold | 物化视图→3年 |
| `fatigue_damage` | 365天 | 90天→cold | 物化视图→5年 |
| `alarm_record` | 180天 | 60天→cold | - |

物化视图 `cavitation_status_daily_mv` / `fatigue_damage_daily_mv` 自动将日统计写入归档表。

## Prometheus监控指标

| 指标 | 类型 | 说明 |
|------|------|------|
| `cavitation_udp_packets_total` | Counter | 接收UDP包总数 |
| `cavitation_fft_computed_total` | Counter | FFT计算总数 |
| `cavitation_detected_total` | Counter | 空化检测次数 |
| `cavitation_alarms_fired_total` | Counter | 告警触发次数 |
| `cavitation_iec61850_sent_total` | Counter | IEC 61850推送次数 |
| `cavitation_queue_depth` | Gauge | MPSC队列深度 |
| `cavitation_pipeline_latency_ms` | Histogram | 管线端到端延迟 |
| `cavitation_active_blades` | Gauge | 活跃空化叶片数 |
| `cavitation_model_retrain_total` | Counter | 模型重训次数 |

## 日志

C++服务使用spdlog，同时输出到:
- **控制台**: 彩色格式 `[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v`
- **文件**: `logs/cavitation_monitor.log` (10MB轮转，保留5个，异步写入)

```bash
# Docker内查看日志
docker-compose logs -f cavitation-monitor

# 本地查看日志
tail -f backend/logs/cavitation_monitor.log
```

## 本地开发 (无Docker)

```bash
# 启动ClickHouse (Docker)
docker run -d --name clickhouse -p 8123:8123 -p 9000:9000 \
  -v $(pwd)/sql/init_clickhouse.sql:/docker-entrypoint-initdb.d/init.sql \
  clickhouse/clickhouse-server:24.1

# 编译C++后端
cd backend && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# 启动后端
./cavitation_monitor --config ../config/model_config.json

# 启动PXI模拟器
python scripts/pxi_simulator.py --target-ip 127.0.0.1 --target-port 9000

# 启动前端
cd frontend && python -m http.server 3000
```
