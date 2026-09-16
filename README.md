# PlantSim / Growth Lab

> 基于物理约束、隐式曲面与可回放生长数据的智能植物生长模拟与交互式编辑系统。

PlantSim 将程序化植物建模、环境约束和生长过程记录整合为一条完整技术链路：系统从 **L-System 骨架** 出发，经由 **Metaball 隐式场** 与 **Marching Cubes** 生成枝干表面；再使用生长时间轴、动态分枝、资源条件、向光性和向地性驱动结构演化；最后把每个生长时间步的完整植物状态与指标保存下来，实现回放、曲线分析和数据导出。

## 特色功能概览

- **植物结构与骨架生成**：节点树、枝干、叶片、根系、芽点等数据模型；L-System 重写和三维海龟解释器；内置松树、柳树、樱花树、灌木等预设。
- **隐式曲面与网格**：Metaball 点源/线段源融合、三维标量场采样、Marching Cubes 网格提取、梯度法向、水密性检查、Taubin 平滑和多级 LOD。
- **连续与离散生长**：年龄时间轴、长度/半径/叶片连续生长曲线、动态分枝、叶片萌发、资源限制、顶端优势和衰败。
- **物理与环境约束**：多光源、受光率、遮挡近似、向光性、向地性、风场、水分、营养和温度参数。
- **生长数据记录与回放**：每个有效时间步保存完整 `PlantModel` 快照及统计指标；支持拖动定位、继续播放、分支截断和关键阶段跳转。
- **交互控制台**：Vue 3 前端提供生长时间轴、播放速度、阶段跳转、指标卡片、SVG 曲线、JSON / CSV 指标导出和离线回放回退。
- **引擎互联**：Qt WebSocket 服务监听 `ws://127.0.0.1:4317`，在 C++ 引擎与浏览器控制台之间传输环境、播放控制和生长指标数据。
- **PBD 植物物理（第 13 周）**：每个植物节点对应一个质量点，对枝干长度、弯曲和分枝夹角进行位置约束求解；提供固定根节点、风力驱动、误差统计与 OpenGL 调试可视化。

## 系统架构

```text
┌──────────────────────────────────────────────────────────────┐
│ Vue 3 控制台                                                   │
│ 时间轴 / 曲线 / 指标 / 导出 / 交互工具                         │
└───────────────────┬──────────────────────────────────────────┘
                    │ WebSocket (ws://127.0.0.1:4317)
┌───────────────────▼──────────────────────────────────────────┐
│ Qt + OpenGL 主程序                                             │
│ EngineWindow / Renderer / WebSocketServer                      │
└───────────────────┬──────────────────────────────────────────┘
                    │
┌───────────────────▼──────────────────────────────────────────┐
│ SimulationEngine                                               │
│ GrowthClock → GrowthTimeline → DynamicBranchingSystem          │
│ EnvironmentParams → Tropism / Resource feedback                │
│ GrowthDataRecorder → 完整快照、回放与指标导出                 │
│ PlantPhysicsSolver → 质量点、PBD 约束与调试数据                 │
└───────┬───────────────────────────┬──────────────────────────┘
        │                           │
┌───────▼────────┐          ┌───────▼──────────────────────────┐
│ PlantModel      │          │ MetaballField → Marching Cubes   │
│ 枝干/叶片/根系  │          │ SurfaceMesh → OpenGL Renderer    │
└────────────────┘          └──────────────────────────────────┘
```

## 核心模块

| 模块 | 主要能力 | 关键位置 |
| --- | --- | --- |
| 植物数据模型 | `PlantNode`、`PlantModel`、枝干/叶片/根系、JSON 序列化 | `include/Plant/`、`src/Plant/` |
| L-System 骨架 | 字符串重写、概率规则、三维海龟解释器、预设 | `src/Algorithm/` |
| 隐式曲面 | 点源和枝段源、标量场、AABB、网格采样 | `src/Implicit/MetaballField.cpp` |
| 网格处理 | Marching Cubes、法向量、Taubin 平滑、LOD、OBJ/MTL | `src/Geometry/` |
| 生长系统 | 时间轴、时钟、连续尺度、生长事件、关键帧 | `include/Engine/Growth*.h`、`src/Engine/` |
| 动态结构 | 分枝生成、叶片萌发、资源和层级约束 | `DynamicBranchingSystem` |
| 环境向性 | 多光源、遮挡近似、向光性、向地性 | `EnvironmentParams` |
| 数据记录与回放 | 完整帧快照、指标统计、最近帧定位、JSON/CSV 导出 | `GrowthDataRecorder` |
| 植物物理 | 质量点、长度/弯曲/夹角 PBD 约束、固定根、误差统计 | `PlantPhysicsSolver` |
| 渲染与网络 | Qt OpenGL 渲染、多光源、WebSocket 控制与广播 | `src/Rendering/`、`src/Networking/` |
| Web 控制台 | 时间轴、曲线、离线回放、导出、三维交互预览 | `frontend/` |

## 第 12 周：生长数据记录与回放

本阶段新增 `GrowthDataRecorder`，把生长时间轴从单纯的实时动画扩展为可检索、可恢复、可导出的数据序列。

### 每帧记录内容

每个有效生长时间步保存一个 `GrowthDataFrame`：

```text
step / age / lifeStage
metrics: height / totalBranchLength / branchCount / leafCount / canopyWidth
plantState: PlantModel::toJson() 完整快照
```

统计口径如下：

| 指标 | 计算方式 |
| --- | --- |
| 植物高度 | 活动枝干节点和活动叶片在 Y 轴上的包围盒高度 |
| 枝干总长度 | 活动父子节点之间的几何长度；退化时回退到节点 `length` |
| 分枝数量 | 活动父子枝干连接数 |
| 叶片数量 | 活动叶片数 |
| 冠幅 | 枝干节点和叶片在 X-Z 平面的最大跨度 |

### 回放行为

- 拖动时间轴会恢复**最接近目标年龄**的完整植物快照，并暂停播放。
- 从历史帧继续播放时，未来帧会被截断，形成新的生长记录分支，避免旧未来数据与新模拟混合。
- 恢复快照时不会再次应用连续尺度，避免枝干和叶片在重复回放中出现几何漂移。
- 回放定位使用较粗的曲面预览；常规播放使用引擎的常规曲面重建设置，以兼顾交互响应与显示效果。

详细说明见 [docs/第12周-生长数据记录与回放.md](docs/第12周-生长数据记录与回放.md)。

## WebSocket 协议

### 前端 → 引擎

```jsonc
{ "type": "growth_start" }
{ "type": "growth_pause" }
{ "type": "growth_resume" }
{ "type": "growth_reset" }
{ "type": "growth_seek", "age": 2.5 }
{ "type": "growth_stage", "stage": "mature" }
{ "type": "growth_speed", "speed": 2.0 }
{ "type": "request_growth_data" }
```

关键阶段标识：`seed`、`sprout`、`growing`、`mature`、`aging`。

### 引擎 → 前端

`growth_state` 用于刷新时间轴、播放状态和指标卡片：

```jsonc
{
  "type": "growth_state",
  "age": 2.5,
  "lifeStage": "vegetative",
  "mode": 0,
  "speed": 1.0,
  "nodeCount": 128,
  "branchCount": 127,
  "leafCount": 56,
  "height": 3.42,
  "totalBranchLength": 18.76,
  "canopyWidth": 2.85,
  "recordedFrameCount": 76,
  "recordedEndAge": 2.5,
  "plantState": { "schema": "plantsim.skeleton", "skeleton": { "nodes": [], "leaves": [] } }
}
```

`growth_data` 返回轻量指标序列，供曲线绘制和浏览器导出：

```jsonc
{
  "type": "growth_data",
  "schema": "plantsim.growth_recording",
  "version": 1,
  "frameCount": 76,
  "frames": [
    {
      "step": 0,
      "age": 0.0,
      "lifeStage": "seedling",
      "metrics": {
        "height": 0.0,
        "totalBranchLength": 0.0,
        "branchCount": 0,
        "leafCount": 0,
        "canopyWidth": 0.0
      },
      "plantState": {
        "schema": "plantsim.skeleton",
        "skeleton": { "nodes": [], "leaves": [] }
      }
    }
  ]
}
```

## 目录结构

```text
├── CMakeLists.txt
├── include/
│   ├── Plant/                         # 植物数据模型
│   ├── Algorithm/                     # L-System 与骨架
│   ├── Implicit/                      # 隐式场
│   ├── Geometry/                      # 网格、叶片、导出
│   └── Engine/                        # 时间轴、回放、环境、动态生长
├── src/
│   ├── Plant/ Algorithm/ Implicit/ Geometry/
│   ├── Engine/                        # SimulationEngine、GrowthDataRecorder 等
│   ├── Rendering/                     # OpenGL 渲染
│   ├── Networking/                    # WebSocketServer
│   └── Tools/                         # 回归与可视化调试程序
├── frontend/
│   ├── src/App.vue                    # 生长数据实验台 UI
│   ├── src/components/PlantViewport.vue
│   └── scripts/websocket-smoke.mjs
├── docs/                              # 第 4 周至第 12 周周报
├── examples/                          # 示例骨架、网格和导出物
└── schemas/                           # JSON Schema
```

## 环境与构建

### 依赖

| 类别 | 版本 / 配置 |
| --- | --- |
| C++ | C++17 |
| 编译器 | Visual Studio 2022 / MSVC |
| GUI 与网络 | Qt 5.14.2 `msvc2017_64` |
| 渲染 | OpenGL 4.3+ |
| 数学库 | Eigen 3.4.0（`third_party/eigen`） |
| 前端 | Node.js 18+、Vue 3、TypeScript、Vite |

默认 Qt 路径：`D:\Qt\5.14.2\msvc2017_64`。

### 配置和构建 C++ 引擎

```powershell
cmake -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=D:/Qt/5.14.2/msvc2017_64 `
  -S . -B build

cmake --build build --config Release
```

也可直接使用 Visual Studio 解决方案：

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  build\PlantSimulationSystem.sln `
  /t:PlantSimulationSystem `
  /p:Configuration=Debug `
  /m:2 /v:minimal
```

### 启动 C++ 引擎

```powershell
$env:Path = "D:\Qt\5.14.2\msvc2017_64\bin;$env:Path"
.\build\Release\PlantSimulationSystem.exe
```

主程序启动后监听 `ws://127.0.0.1:4317`。

### 启动前端控制台

```powershell
cd frontend
npm install
npm run dev -- --host 127.0.0.1 --port 5173
```

浏览器访问 `http://127.0.0.1:5173`。如需覆盖默认引擎地址：

```powershell
$env:VITE_ENGINE_WS_URL = "ws://127.0.0.1:4317"
npm run dev
```

未启动 C++ 引擎时，前端会自动切换到离线示例数据，以便演示时间轴、曲线和导出功能。

## 验证

### 前端检查

```powershell
cd frontend
npm run build
```

该命令运行 `vue-tsc --noEmit` 和 Vite 生产构建。

### 基础 WebSocket 冒烟测试

启动 C++ 引擎后：

```powershell
cd frontend
npm run smoke
```

### 生长记录手动验证流程

1. 在前端点击播放，确认年龄和记录帧数持续增加。
2. 切换“植物高度 / 枝干总长度 / 分枝数量 / 叶片数量 / 冠幅”曲线。
3. 拖动时间轴到历史年龄，确认播放自动暂停且指标回到对应帧。
4. 点击关键阶段按钮，确认跳转到种子、萌发、营养生长、成熟或老化阶段。
5. 点击 JSON / CSV 导出，确认下载的指标序列包含 `step`、`age`、生命周期和五项指标。

## PBD 植物物理

主程序默认关闭物理求解，因此第 12 周的生长记录与回放保持确定性。在 Qt 窗口的 **VIEWPORT** 区域可按需打开：

1. 选中 `Enable PBD physics` 以启用 60 Hz 物理步进。
2. 选中 `Physics debug overlay` 查看约束结构和统计。
3. 青色线为长度约束，橙色折线为弯曲弦长约束，紫色为分枝夹角约束；红色十字表示固定根节点，绿色十字表示可运动质量点。

物理回归程序可单独构建与运行：

```powershell
$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild build\PlantSimulationSystem.sln /t:PlantPhysicsDemo /p:Configuration=Debug /m:2 /v:minimal
.\build\Debug\PlantPhysicsDemo.exe
```

测试程序覆盖质量点构建、固定根、三类约束、写回 `PlantModel` 及调试快照。详见[第 13 周：植物物理模型](docs/第13周-植物物理模型.md)。

## 开发进度

| 周次 | 里程碑 | 主要交付 |
| --- | --- | --- |
| 第 4 周 | 植物数据结构 | `PlantNode` / `PlantModel`、JSON、结构验证工具 |
| 第 5 周 | L-System 植物骨架生成 | 重写规则、三维海龟解释器、预设 |
| 第 6 周 | Metaball 隐式曲面 | 标量场、场源构建、切片可视化 |
| 第 7 周 | Marching Cubes 网格提取 | 网格、法向量、水密校验 |
| 第 8 周 | 模型优化与叶片生成 | 平滑、Taubin、LOD、叶片、OBJ/MTL |
| 第 9 周 | 生长时间模型 | 时间轴、连续生长曲线、时钟控制 |
| 第 10 周 | 动态分枝生成 | 分枝/叶片事件、资源与关键帧 |
| 第 11 周 | 向光性与向地性 | 多光源、遮挡近似、环境向性控制 |
| 第 12 周 | 生长数据记录与回放 | 完整状态帧、时间轴回放、指标曲线、数据导出 |
| 第 13 周 | 植物物理模型 | 质量点、长度/弯曲/夹角约束、调试模式 |

## 文档

- [需求分析文档](需求分析文档.md)
- [技术栈文档](技术栈文档.md)
- [详细设计文档](详细设计文档.md)
- [第 4 周：植物数据结构](docs/第4周-植物数据结构.md)
- [第 5 周：L-System 植物骨架生成](docs/第5周-L-System植物骨架生成.md)
- [第 6 周：Metaball 隐式曲面](docs/第6周-Metaball隐式曲面.md)
- [第 7 周：Marching Cubes 网格提取](docs/第7周-MarchingCubes网格提取.md)
- [第 8 周：模型优化与叶片生成](docs/第8周-模型优化与叶片生成.md)
- [第 9 周：生长时间模型](docs/第9周-生长时间模型.md)
- [第 10 周：动态分枝生成](docs/第10周-动态分枝生成.md)
- [第 11 周：向光性与向地性](docs/第11周-向光性与向地性.md)
- [第 12 周：生长数据记录与回放](docs/第12周-生长数据记录与回放.md)
- [第 13 周：植物物理模型](docs/第13周-植物物理模型.md)
- [第 14 周：交互式编辑](docs/week14-report.md)
