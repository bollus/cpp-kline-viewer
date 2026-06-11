# Q4J K-Line Viewer 通用 Layers 协议

策略 overlay 接口可以返回通用 `layers`，前端会直接按图形协议渲染，不需要为每个策略定制字段解析。

## 响应格式

接口：

`GET /api/strategy-overlay-events`

查询参数：

| 参数 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `strategy` | string | 是 | 策略名。 |
| `symbol` | string | 是 | 交易品种。 |
| `interval` | string | 是 | 当前图表周期，例如 `1m`、`30m`。 |
| `startTime` | number | 是 | 查询开始时间，毫秒时间戳。 |
| `endTime` | number | 是 | 查询结束时间，毫秒时间戳。 |

客户端不再发送 `higherInterval` / `lowerInterval`，策略需要的内部周期应由后端按策略配置或当前 `interval` 自行决定。

推荐返回：

```json
{
  "version": 2,
  "strategy": "n_in_range_variant",
  "symbol": "XAUUSD",
  "interval": "1m",
  "la[CMakeLists.txt](../../../CMakeLists.txt)yers": []
}
```

旧版事件数组仍兼容，但新策略建议全部返回对象格式。

## 协议版本变更

### v2

- `position.pnl` 改为 `position.totalR`。`totalR` 表示该持仓按分批平仓比例加权后的总 R，不是每次平仓 R 的简单累加。
- `position.tp1`、`position.tp2` 改为 `position.tps` 数组。策略可以只给一个 TP，也可以给超过两个 TP。
- 新增 `position.remark`，用于承载策略对持仓区块的文字说明。旧的 `background`、`signal` 解释字段不再推荐使用，统一迁移到 `remark`。
- `remark` 可能较长，客户端 hover 面板只展示换行后的预览；如果需要右键复制完整备注或结构化说明，请同时放入 `position.data`。
- 客户端仍兼容读取旧的 `pnl/tp1/tp2`，但新接入策略必须按 v2 输出 `totalR/tps`。

`totalR` 计算示例：

如果一笔交易分两次平仓：50% 仓位在 `+1R` 平仓，50% 仓位在 `+3R` 平仓，则：

```text
totalR = 1 * 0.5 + 3 * 0.5 = 2R
```

不能计算成：

```text
1R + 3R = 4R
```

## 基础概念

### 坐标

- 时间统一使用毫秒时间戳，例如 `1780007100000`。
- 价格统一使用数字，例如 `4500.25`。
- 图形位置都以 `time + price` 或 `from/to + top/bottom` 表达。
- 字段名区分大小写，建议严格按本文档输出。

### 公共字段

每个 layer 都支持这些字段：

```json
{
  "id": "unique-id",
  "type": "box",
  "group": "iFVG",
  "visible": true,
  "zIndex": 20,
  "style": {},
  "data": {}
}
```

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `id` | string | 推荐必填 | 稳定唯一 ID，用于前端合并多次可见范围请求。缺失时前端会用内容生成临时 ID，但不建议依赖。 |
| `type` | string | 是 | 图形类型。当前支持 `line`、`polyline`、`ray`、`box`、`range`、`priceLine`、`label`、`marker`、`position`。 |
| `group` | string | 否 | 左上角 Layers 面板中的分组名。为空时使用 `Default`。 |
| `visible` | boolean | 否 | 初始是否显示，默认 `true`。 |
| `zIndex` | number | 否 | 绘制层级，越大越晚绘制，默认 `0`。 |
| `style` | object | 否 | 样式配置。 |
| `data` | object | 否 | 业务原始数据。`position` 右键复制时优先复制该字段。 |

### 公共样式字段

```json
{
  "stroke": "#93c5fd",
  "strokeWidth": 1,
  "strokeOpacity": 1,
  "fill": "#6ed7f6",
  "fillOpacity": 0.5,
  "dash": [4, 4],
  "textColor": "#ffffff",
  "fontSize": 11
}
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `stroke` | string | 线条颜色，使用 CSS hex，例如 `#93c5fd`。 |
| `strokeWidth` | number | 线条宽度，前端会限制在 `1-8`。 |
| `strokeOpacity` | number | 线条透明度，范围 `0-1`。 |
| `fill` | string | 填充颜色。 |
| `fillOpacity` | number | 填充透明度，范围 `0-1`。 |
| `dash` | number[] | 虚线段配置，例如 `[4, 4]`。部分水平线会按虚线渲染，但不保证完全按数组比例。 |
| `textColor` | string | 文本颜色。 |
| `fontSize` | number | 文本或 marker 尺寸，前端会限制在 `8-22`。 |

### 持仓区块专用样式字段

```json
{
  "profitFill": "#20c997",
  "lossFill": "#ef5f78",
  "entryLine": "#374151",
  "slLine": "#ef4444",
  "tpLine": "#10b981",
  "fillOpacity": 0.32
}
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `profitFill` | string | 盈利区块填充色。 |
| `lossFill` | string | 亏损区块填充色。 |
| `entryLine` | string | Entry 线颜色。 |
| `slLine` | string | SL 线颜色。 |
| `tpLine` | string | 所有 TP 线颜色。 |
| `fillOpacity` | number | 盈亏区域透明度。 |

## line / polyline / ray

### 用途

用于画 N 结构、趋势腿、波段连接线、突破路径、策略内部结构线。

### 字段

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `type` | string | 是 | `line`、`polyline` 或 `ray`。当前 `ray` 按普通线段渲染。 |
| `points` | array | 是 | 至少两个点，每个点包含 `time` 和 `price`。 |
| `text` | string | 否 | 显示在最后一个点附近的文本。 |
| `style.stroke` | string | 否 | 线条颜色。 |
| `style.strokeWidth` | number | 否 | 线条粗细。 |
| `style.dash` | number[] | 否 | 虚线配置。 |

### 渲染行为

- `line` 和 `polyline` 都会按 `points` 顺序连接。
- `text` 会显示在最后一个点附近。
- 只要所有点的时间范围与当前可见范围有交集，就参与绘制。
- 鼠标 hover 到线段附近时，线段会加粗高亮，并显示每个 `points` 点位的价格。
- 左键点击线段会切换常亮模式；再次点击同一线段会取消常亮。
- 常亮状态按 `id` 记录，因此线段 `id` 必须稳定。接口刷新后同一 `id` 会继续保持常亮。

### 注意事项

- `points` 中不要混入无效时间或非数字价格，无效点会导致该点被忽略或图形不可见。
- 对于只需要两点的线段，用 `line`；对于三点以上结构，用 `polyline`。
- `id` 应该包含结构类型和关键时间，例如 `n-leg-1780007100000`。

### 示例

```json
{
  "id": "n-leg-001",
  "type": "polyline",
  "group": "N Structure",
  "points": [
    { "time": 1780007100000, "price": 4500.2 },
    { "time": 1780007400000, "price": 4508.6 },
    { "time": 1780007700000, "price": 4504.1 }
  ],
  "text": "N",
  "style": {
    "stroke": "#f0b64f",
    "strokeWidth": 2,
    "dash": [4, 4]
  }
}
```

## box

### 用途

用于画 FVG、IFVG、供需区、订单块、策略识别出的矩形区域。

### 字段

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `type` | string | 是 | `box`。 |
| `from` | number | 是 | 矩形开始时间。 |
| `to` | number | 是 | 矩形结束时间。 |
| `top` | number | 是 | 矩形上边价格。 |
| `bottom` | number | 是 | 矩形下边价格。 |
| `text` | string | 否 | 显示在左上角附近的文本；为空时默认使用 `group`。 |
| `style.fill` | string | 否 | 填充色。 |
| `style.fillOpacity` | number | 否 | 填充透明度。 |
| `style.stroke` | string | 否 | 边框颜色。 |

### 渲染行为

- `from/to` 决定横向范围。
- `top/bottom` 决定纵向范围，前端会自动归一化上下关系。
- `zIndex` 建议低于 marker/label，但高于普通网格。

### 注意事项

- IFVG 推荐统一使用蓝色透明背景和淡蓝边框，例如 `fill=#6ed7f6`、`fillOpacity=0.5`、`stroke=#93c5fd`。
- `to` 不要多偏移一根 K 线；如果区域截止到某根 K 线开盘时间，就直接用该时间。
- 如果 box 跨越很长历史，后端仍建议按请求窗口返回附近相关 box，避免无意义数据过大。

### 示例

```json
{
  "id": "ifvg-001",
  "type": "box",
  "group": "iFVG",
  "from": 1780007100000,
  "to": 1780007400000,
  "top": 4510.5,
  "bottom": 4506.2,
  "text": "iFVG",
  "style": {
    "stroke": "#93c5fd",
    "fill": "#6ed7f6",
    "fillOpacity": 0.5
  }
}
```

## range

### 用途

用于画震荡区间、高低点区间、可被打击的 range 边界。

### 字段

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `type` | string | 是 | `range`。 |
| `from` | number | 是 | 开始时间。 |
| `to` | number | 是 | 结束时间。 |
| `price` | number | 条件必填 | 只画单条水平区间线时使用。 |
| `top` | number | 条件必填 | 画区域时使用。 |
| `bottom` | number | 条件必填 | 画区域时使用。 |
| `text` | string | 否 | 标签文本，例如 `HIGH`、`LOW`。 |

### 渲染行为

- 只提供 `price` 时，渲染为水平线。
- 提供 `top/bottom` 时，渲染为矩形区域。

### 注意事项

- HIGH/LOW 两条线建议用两个独立 layer，方便单独定位和复制。
- 被打击、触碰、突破这类事件建议另用 `marker` 或 `label` 表达，不要把语义塞进 `range`。

### 示例

```json
{
  "id": "range-high-001",
  "type": "range",
  "group": "Range",
  "from": 1780007100000,
  "to": 1780008600000,
  "price": 4512.3,
  "text": "HIGH",
  "style": {
    "stroke": "#f59e0b",
    "dash": [4, 4]
  }
}
```

## priceLine

### 用途

用于画独立价格线，例如 entry line、关键价位、止损价、目标价、当前策略参考价。

### 字段

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `type` | string | 是 | `priceLine`。前端内部会转成小写处理。 |
| `price` | number | 是 | 价格。 |
| `from` | number | 否 | 起始时间，默认当前缓存第一根 K 线时间。 |
| `to` | number | 否 | 结束时间，默认当前最新 K 线后延一段。 |
| `text` | string | 否 | 显示在线末尾附近。 |

### 注意事项

- 如果价格线属于某个持仓区块，优先使用 `position`，不要额外输出多条 priceLine，避免重复。
- 如果只想画 range 边界，`range` 更语义化。

### 示例

```json
{
  "id": "ref-price-001",
  "type": "priceLine",
  "group": "Reference",
  "price": 4507.25,
  "text": "Ref",
  "style": {
    "stroke": "#72d9f7",
    "dash": [3, 3]
  }
}
```

## label

### 用途

用于画文字标注，例如策略状态、结构名称、原因说明、命中结果。

### 字段

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `type` | string | 是 | `label`。 |
| `from` | number | 是 | 标签锚点时间。 |
| `price` | number | 是 | 标签锚点价格。 |
| `text` | string | 是 | 标签文本。 |
| `anchor` | string | 否 | 当前主要支持 `center`；其他值会按默认偏移显示。 |
| `style.fill` | string | 否 | 标签背景色。 |
| `style.textColor` | string | 否 | 文本颜色。 |

### 渲染行为

- 默认显示在锚点右上附近。
- `anchor=center` 时，标签居中覆盖锚点。

### 注意事项

- 不要用过长文本；图表空间有限，建议短标签。
- 大量 label 会影响可读性和性能，能用 marker 表达的短事件优先用 marker。
- 如果 label 属于某个区域，建议和区域使用同一个 `group`。

### 示例

```json
{
  "id": "label-001",
  "type": "label",
  "group": "Signal",
  "from": 1780007500000,
  "price": 4512.0,
  "text": "Breakout",
  "anchor": "center",
  "style": {
    "stroke": "#20c997",
    "fill": "#0b100f",
    "textColor": "#ffffff"
  }
}
```

## marker

### 用途

用于画紧凑事件点，例如突破、触碰、入场、离场、fractals、range 被打击标识。

### 字段

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `type` | string | 是 | `marker`。 |
| `from` | number | 是 | 标记时间。 |
| `price` | number | 是 | 标记价格。 |
| `shape` | string | 否 | `circle`、`square`、`triangle-up`、`triangle-down`，默认 `circle`。 |
| `text` | string | 否 | 显示在 marker 右侧的短文本。 |
| `style.stroke` | string | 否 | marker 颜色。 |
| `style.fontSize` | number | 否 | marker 大小和文字大小参考值。 |

### 渲染行为

- `circle` 默认画圆点。
- `triangle-up` / `triangle-down` 用于方向性信号。
- `text` 会显示在 marker 右侧。

### 注意事项

- Entry / Exit 如果是持仓的一部分，优先使用 `position` 自带标记能力。
- Fractals 一类上下标记建议用 `triangle-up` 和 `triangle-down`，并把价格放在 wick 外侧一点的位置。
- 同一根 K 线上多个 marker 可能重叠，后端可通过略微调整 `price` 或拆 group 控制可读性。

### 示例

```json
{
  "id": "break-001",
  "type": "marker",
  "group": "Signal",
  "from": 1780007500000,
  "price": 4512.0,
  "shape": "triangle-up",
  "text": "Break",
  "style": {
    "stroke": "#20c997",
    "fontSize": 10
  }
}
```

## position

### 用途

用于画完整持仓区块，包括 entry、SL、任意数量 TP、盈利区、亏损区、totalR、remark、hover 和右键复制。

### 字段

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `type` | string | 是 | `position`。 |
| `side` | string | 是 | `long` 或 `short`。非 `short` 会按 long 处理。 |
| `entryTime` | number | 是 | 开仓时间。也可用 `from` 兜底。 |
| `exitTime` | number | 否 | 平仓/结束时间。也可用 `to` 兜底；缺失时前端延续到最新 K 线后。 |
| `entry` | number | 是 | 开仓价格。 |
| `sl` | number | 推荐 | 止损价格。没有 SL 时不会显示亏损区，也无法计算 1R。 |
| `tps` | number[] | 否 | 目标价数组。可以是 1 个、2 个或更多 TP，例如 `[4505, 4510, 4518]`。也兼容对象数组如 `[{"price":4505}]`。 |
| `quantity` | number | 否 | 数量，用于复制/展示。 |
| `amount` | number | 否 | 金额，用于业务数据。 |
| `totalR` | number | 否 | 按每次平仓比例加权后的总 R，用于 hover 展示和无 `data` 时的复制 JSON。正数按盈利色显示，负数按亏损色显示。 |
| `remark` | string | 否 | 策略备注/说明。内容可以较长，前端会在持仓 hover 面板中换行展示并截断预览。 |
| `data` | object | 推荐 | 右键复制优先复制该对象。 |

### 渲染行为

- Long：`tps` 中有效目标价应高于 entry，`sl` 应低于 entry。
- Short：`tps` 中有效目标价应低于 entry，`sl` 应高于 entry。
- 盈利区使用 `profitFill`，亏损区使用 `lossFill`。
- `tps` 前面的目标位使用虚线，最后一个目标位使用实线。
- 会在 entry 位置画 L/S 标记；该标记跟随 position 所属 layer group 的显隐状态。
- hover 持仓区块时会展示 entry、SL、1R TP、所有 TP 对应的 R、Total R 和 `remark`。
- `remark` 较长时只在 hover 面板中显示预览；完整备注建议同时放进 `data`，用于右键复制。
- 右键点击持仓区块可复制信息。

### 右键复制

如果存在 `data`，复制内容就是格式化后的 `data`。建议后端放入策略需要复盘/分享的完整业务信息。

推荐最小 `data`：

```json
{
  "entry": 4500.0,
  "sl": 4495.0,
  "1r_tp": 4505.0,
  "totalR": 2.0
}
```

如果没有 `data`，前端会根据 `entry/sl` 和区间内 K 线生成基础复制 JSON。

### 注意事项

- `id` 必须稳定，建议用订单 ID 或 `position-{entryTime}-{side}`。
- 不要额外输出 entry/sl/tp 的 priceLine，除非你明确需要重复强调；`position` 已经会画这些线。
- 已平仓仓位应给 `exitTime`，否则区块会延续到最新时间附近。
- `tps` 不符合 long/short 方向时可能不会形成有效盈利区。
- `totalR` 必须按每次平仓比例加权计算。例如 30% 在 1R、30% 在 2R、40% 在 -0.5R，则 `totalR = 1*0.3 + 2*0.3 + (-0.5)*0.4 = 0.7R`。
- `data` 不参与渲染，只用于复制，因此可以包含策略名、原因、K线切片、信号详情等。
- 不要再为持仓区块输出旧的 `background`、`signal` 字段；需要给策略保存解释性信息时使用 `remark`，需要完整业务结构时使用 `data`。

### 示例

```json
{
  "id": "position-001",
  "type": "position",
  "group": "Position",
  "side": "long",
  "entryTime": 1780007100000,
  "exitTime": 1780008600000,
  "entry": 4500.0,
  "sl": 4495.0,
  "tps": [4505.0, 4510.0, 4518.0],
  "quantity": 1,
  "amount": 1000,
  "totalR": 2.15,
  "remark": "IFVG continuation entry; higher timeframe remains bullish; partial exit was filled near TP1.",
  "style": {
    "profitFill": "#20c997",
    "lossFill": "#ef5f78",
    "fillOpacity": 0.32,
    "entryLine": "#374151",
    "slLine": "#ef4444",
    "tpLine": "#10b981"
  },
  "data": {
    "entry": 4500.0,
    "sl": 4495.0,
    "1r_tp": 4505.0,
    "tps": [4505.0, 4510.0, 4518.0],
    "totalR": 2.15,
    "remark": "IFVG continuation entry; higher timeframe remains bullish; partial exit was filled near TP1.",
    "reason": "IFVG continuation entry"
  }
}
```

## 完整响应示例

```json
{
  "version": 2,
  "strategy": "example_strategy",
  "symbol": "XAUUSD",
  "interval": "1m",
  "layers": [
    {
      "id": "ifvg-1780007100000",
      "type": "box",
      "group": "iFVG",
      "from": 1780007100000,
      "to": 1780007400000,
      "top": 4510.5,
      "bottom": 4506.2,
      "text": "iFVG",
      "zIndex": 10,
      "style": {
        "stroke": "#93c5fd",
        "fill": "#6ed7f6",
        "fillOpacity": 0.5
      }
    },
    {
      "id": "break-1780007500000",
      "type": "marker",
      "group": "Signal",
      "from": 1780007500000,
      "price": 4512.0,
      "shape": "triangle-up",
      "text": "Break",
      "zIndex": 30,
      "style": {
        "stroke": "#20c997",
        "fontSize": 10
      }
    },
    {
      "id": "position-1780007600000-long",
      "type": "position",
      "group": "Position",
      "side": "long",
      "entryTime": 1780007600000,
      "exitTime": 1780008600000,
      "entry": 4508.0,
      "sl": 4503.0,
      "tps": [4513.0, 4518.0],
      "totalR": 1.5,
      "remark": "Breakout continuation after iFVG confirmation.",
      "zIndex": 20,
      "data": {
        "entry": 4508.0,
        "sl": 4503.0,
        "1r_tp": 4513.0,
        "tps": [4513.0, 4518.0],
        "totalR": 1.5,
        "remark": "Breakout continuation after iFVG confirmation."
      }
    }
  ]
}
```

## 生成建议

- 每个 layer 的 `id` 必须稳定，同一策略同一图形不要每次请求生成新 ID。
- 同类图形使用同一个 `group`，用户可以在图表左上角统一开关。
- 大量图形应按当前请求的 `startTime/endTime` 返回附近数据，前端会按可见时间过滤绘制。
- 新策略不要再依赖旧的 `eventType/payloadJson` 字段。
- 对于同一语义，不要同时用多个 layer 重复表达，除非用户确实需要叠加视觉效果。
- 如果你不确定该用哪种类型：区域用 `box`，事件点用 `marker`，说明文字用 `label`，交易用 `position`，价格水平线用 `priceLine`。
