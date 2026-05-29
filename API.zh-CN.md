# Q4J K-Line Viewer 后端 API 文档

这份文档说明如何为 `q4j_kline_viewer` 实现自己的后端接口。

所有时间戳均为 Unix epoch 毫秒。价格和数量字段使用 JSON number，可以是整数或浮点数。

## 后端地址

程序启动时会要求填写：

```text
HTTP 后端: http://127.0.0.1:8080
WebSocket: ws://127.0.0.1:8080
```

如果 WebSocket 地址留空，程序会根据 HTTP 地址自动推导。

## 错误响应格式

接口错误时，建议返回非 2xx HTTP 状态码，并返回 JSON 对象：

```json
{
  "message": "Ctrade symbol not found: AAPL, available symbols sample: [EURUSD, GBPUSD, USDJPY]"
}
```

当初始 K 线接口失败时，程序会清空图表，并把 `message` 显示在 K 线图区域中。

## K 线接口

### GET `/api/candles`

用于初始 K 线加载和历史 K 线回拉。

请求参数：

| 参数 | 必填 | 示例 | 说明 |
| --- | --- | --- | --- |
| `symbol` | 是 | `XAUUSD` | 交易品种。 |
| `interval` | 是 | `1m` | K 线周期。建议后端兼容大小写。 |
| `startTime` | 是 | `1716900000000` | 开始时间，毫秒。 |
| `endTime` | 是 | `1716903600000` | 结束时间，毫秒。 |

响应：

```json
[
  {
    "timestamp": 1716900000000,
    "open": 2350.12,
    "high": 2351.44,
    "low": 2349.80,
    "close": 2350.90
  }
]
```

字段说明：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `timestamp` | number | K 线开盘时间，毫秒。 |
| `open` | number | 开盘价。 |
| `high` | number | 最高价。 |
| `low` | number | 最低价。 |
| `close` | number | 收盘价。 |

请按 `timestamp` 升序返回。

## 实时 K 线 WebSocket

### WebSocket `/ws/candles`

程序会连接：

```text
/ws/candles?symbol=XAUUSD&interval=1m
```

每条消息发送一根 K 线 JSON：

```json
{
  "timestamp": 1716900060000,
  "open": 2350.90,
  "high": 2352.10,
  "low": 2350.50,
  "close": 2351.70
}
```

如果 `timestamp` 已存在，程序会更新这根 K 线；否则会插入新 K 线。

用户可以在后端设置弹窗里关闭实时 K 线。关闭后程序不会连接该 WebSocket。

## 策略图层事件接口

### GET `/api/strategy-overlay-events`

用于绘制 Range、N、N-IN、iFVG、持仓区块和订单标注。

请求参数：

| 参数 | 必填 | 示例 | 说明 |
| --- | --- | --- | --- |
| `strategy` | 是 | `n_in_range_variant` | 程序当前固定传这个策略名。 |
| `symbol` | 是 | `XAUUSD` | 交易品种。 |
| `higherInterval` | 是 | `15m` | 策略高周期。 |
| `lowerInterval` | 是 | `1m` | 策略低周期。 |
| `startTime` | 是 | `1716900000000` | 查询开始时间，毫秒。 |
| `endTime` | 是 | `1716903600000` | 查询结束时间，毫秒。 |

响应：

```json
[
  {
    "id": "evt-001",
    "eventType": "RANGE_BOUNDARY_UPDATED",
    "eventTime": 1716900000000,
    "direction": "LONG",
    "price": 2350.90,
    "payloadJson": "{\"point\":{\"side\":\"HIGH\",\"time\":1716900000000,\"display_time\":1716900000000,\"price\":2358.0}}"
  }
]
```

顶层字段：

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `id` | string/number | 建议 | 事件唯一 ID，用于去重。 |
| `eventType` | string | 是 | 事件类型。 |
| `eventTime` | number | 是 | 事件时间，毫秒。 |
| `direction` | string | 否 | `LONG` 或 `SHORT`。 |
| `price` | number | 否 | 部分标注的备用价格。 |
| `payloadJson` | string | 是 | JSON 字符串，程序会再次解析它。 |

注意：`payloadJson` 是 JSON 字符串，不是嵌套对象。

如果不需要策略图层，可以直接返回：

```json
[]
```

## 支持的事件类型

### `RANGE_BOUNDARY_UPDATED`

绘制震荡区间边界线。

`payloadJson` 内容：

```json
{
  "point": {
    "side": "HIGH",
    "time": 1716900000000,
    "display_time": 1716900000000,
    "price": 2358.0
  }
}
```

字段说明：

| 字段 | 说明 |
| --- | --- |
| `point.side` | 通常是 `HIGH` 或 `LOW`。 |
| `point.time` | 逻辑开始时间。 |
| `point.display_time` | 可选，显示开始时间；没有则使用 `time`。 |
| `point.price` | 边界价格。 |

### `RANGE_BOUNDARY_ENDED`

结束一条区间边界线。

```json
{
  "side": "HIGH",
  "start_time": 1716900000000,
  "end_time": 1716901800000
}
```

### `RANGE_BOUNDARY_TOUCHED`

绘制区间被打击/突破的标记。

```json
{
  "side": "UPPER"
}
```

`side` 为 `UPPER` 时绘制上方突破标记，其他值会按下方突破处理。

### `HIGH_N_DETECTED`

绘制 N 字结构。

```json
{
  "n": {
    "anchor_time": 1716900000000,
    "anchor_display_time": 1716900000000,
    "anchor_price": 2350.0,
    "turning_time": 1716900600000,
    "turning_display_time": 1716900600000,
    "turning_price": 2358.0,
    "retrace_time": 1716901200000,
    "retrace_display_time": 1716901200000,
    "retrace_price": 2353.0,
    "breakout_time": 1716901800000,
    "breakout_close": 2360.0
  }
}
```

`*_display_time` 字段可选。没有时使用对应的普通时间字段。

### `HIGH_N_IN_DETECTED`

绘制 Base N 和 Signal N-IN。

```json
{
  "base_n": { "...": "字段结构同 n" },
  "signal_n": { "...": "字段结构同 n" }
}
```

### `ENTRY_SIGNAL_OPEN_SENT`

用于绘制 iFVG 区块，也用于持仓 tooltip 的背景信息。

```json
{
  "entry_time": 1716901800000,
  "entry_signal": {
    "type": "IFVG",
    "direction": "LONG",
    "time": 1716901800000,
    "fvg": {
      "top": 2356.0,
      "bottom": 2354.5,
      "ifvg_time": 1716901800000,
      "k1": {
        "open_time": 1716901200000
      }
    }
  },
  "background": {
    "type": "N_IN",
    "direction": "LONG"
  }
}
```

iFVG 时间字段的 fallback：

| 优先字段 | 备用字段 |
| --- | --- |
| `entry_signal.fvg.k1.open_time` | `entry_signal.fvg.k1_time`，再用 `entry_signal.fvg.create_time` |
| `entry_signal.fvg.ifvg_time` | `entry_signal.time` |

### `POSITION_OPEN_FILLED`

绘制持仓区块和开仓标注。

```json
{
  "position_id": 1001,
  "entry_time": 1716901800000,
  "exec_price": 2355.0,
  "entry": 2355.0,
  "sl": 2352.0,
  "tp1": 2358.0,
  "quantity": 1.0,
  "entry_signal": {
    "type": "IFVG",
    "direction": "LONG",
    "time": 1716901800000,
    "fvg": {
      "top": 2356.0,
      "bottom": 2354.5,
      "ifvg_time": 1716901800000,
      "k1": { "open_time": 1716901200000 }
    }
  },
  "background": {
    "type": "N_IN",
    "direction": "LONG"
  }
}
```

顶层 `direction` 建议传 `LONG` 或 `SHORT`。

### `POSITION_PARTIAL_CLOSED`

绘制 TP1 标注和 TP1 线。

```json
{
  "position_id": 1001,
  "entry_time": 1716901800000,
  "entry": 2355.0,
  "exit_time": 1716902400000,
  "exit_price": 2358.0,
  "close_pnl": 3.0
}
```

### `POSITION_CLOSED`

绘制最终平仓标注，并结束持仓区块。

```json
{
  "position_id": 1001,
  "entry_time": 1716901800000,
  "entry": 2355.0,
  "exit_time": 1716903000000,
  "exit_price": 2360.0,
  "close_pnl": 5.0,
  "total_pnl": 8.0
}
```

程序优先使用 `position_id` 匹配持仓；如果没有 `position_id`，则使用 `entry_time` 匹配。

## 最小后端实现

如果只想先让程序显示 K 线：

1. 实现 `GET /api/candles`。
2. 可选实现 `/ws/candles`。
3. `GET /api/strategy-overlay-events` 返回 `[]`。

这样已经可以使用 K 线、坐标轴、十字光标、OHLC 示意图、拖拽和缩放。

