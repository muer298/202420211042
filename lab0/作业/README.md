# lab0 标准化图示版本

本目录包含对原始 `lab0.zip` 三张图的重新整理版本。

## 主要修正

1. 总流程图与时钟中断图改为标准“时序图”逻辑：时间自上而下、参与者固定为竖直生命线、每个事件只在自己的水平线上连线，避免跨框、压字和斜线穿越节点。
2. 快照图改为“进程状态 / 地址空间 / 边界说明”三层结构，只保留必要关系线。
3. 原始 `.mmd` 文件中的 Markdown 代码围栏（```mermaid ... ```）已去掉。若使用 Mermaid CLI，`.mmd` 文件本身不要带代码围栏。
4. 原 `task1-02-snapshot.mmd` 中 `subgraph ... ]\n` 的写法已改为普通 subgraph 声明，减少解析器兼容问题。
5. 将父进程 `sh` 与子进程 `echo` 拆成独立参与者，避免 fork 之后父子路径共用一条生命线导致语义和连线混乱。
6. `write` 阶段的 UART TX 睡眠/唤醒标为可选路径，计时器图不再混入 read/write 的阻塞细节。

## 文件

- `source/task1-01-flow_standard.mmd`：总流程 Mermaid 源码
- `source/task1-02-snapshot_standard.mmd`：快照 Mermaid 源码
- `source/task1-03-timer_standard.mmd`：时钟中断 Mermaid 源码
- `source/render_standard_diagrams.py`：可直接生成 PNG + SVG 的 Python 绘图脚本
- `rendered/*.png`：位图预览
- `rendered/*.svg`：矢量图，适合报告/PPT/论文

## 重新生成图片

Python 方式：

```bash
python3 -m pip install matplotlib
python3 source/render_standard_diagrams.py
```

Mermaid CLI 方式（渲染单个 `.mmd`）：

```bash
npx -y @mermaid-js/mermaid-cli \
  -i source/task1-01-flow_standard.mmd \
  -o task1-01-flow_standard.svg \
  -b transparent
```

其余两个 `.mmd` 文件同理。

## 建议

课程作业、实验报告优先使用 SVG；若平台只接收图片，再使用 PNG。不要再次把 Mermaid 时序图转换成自定义“泳道框 + 自动连线”布局，否则容易重新出现跨框和线条错乱。
