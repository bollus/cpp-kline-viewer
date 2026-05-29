# Q4J K-Line Viewer Backend API

This document describes the backend API needed by `q4j_kline_viewer`.

All timestamps are Unix epoch milliseconds. Numeric fields may be integer or floating point JSON numbers.

## Base URLs

The app asks for two URLs at startup:

```text
HTTP Backend: http://127.0.0.1:8080
WebSocket:    ws://127.0.0.1:8080
```

If the WebSocket URL is empty, the app derives it from the HTTP URL.

## Error Format

For API errors, return a non-2xx status code and a JSON object with `message`.

```json
{
  "message": "Ctrade symbol not found: AAPL, available symbols sample: [EURUSD, GBPUSD, USDJPY]"
}
```

The app clears the chart and shows this message in the chart area when the initial candle request fails.

## Candles

### GET `/api/candles`

Used for initial load and historical backfill.

Query parameters:

| Name | Required | Example | Description |
| --- | --- | --- | --- |
| `symbol` | yes | `XAUUSD` | Market symbol. |
| `interval` | yes | `1m` | Candle interval. Treat values case-insensitively if possible. |
| `startTime` | yes | `1716900000000` | Inclusive start time in ms. |
| `endTime` | yes | `1716903600000` | Inclusive/exclusive end time in ms. Either is OK if consistent. |

Response:

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

Required candle fields:

| Field | Type | Description |
| --- | --- | --- |
| `timestamp` | number | Candle open time in ms. |
| `open` | number | Open price. |
| `high` | number | High price. |
| `low` | number | Low price. |
| `close` | number | Close price. |

Return candles sorted ascending by `timestamp`.

## Realtime Candles

### WebSocket `/ws/candles`

The app connects to:

```text
/ws/candles?symbol=XAUUSD&interval=1m
```

Each text message should be one candle JSON object:

```json
{
  "timestamp": 1716900060000,
  "open": 2350.90,
  "high": 2352.10,
  "low": 2350.50,
  "close": 2351.70
}
```

If `timestamp` matches an existing candle, the app updates that candle. Otherwise, it inserts it.

Realtime can be disabled in the backend settings dialog; when disabled, this WebSocket is not used.

## Strategy Overlay Events

### GET `/api/strategy-overlay-events`

Used to draw Range, N, N-IN, iFVG, positions, and markers.

Query parameters:

| Name | Required | Example | Description |
| --- | --- | --- | --- |
| `strategy` | yes | `n_in_range_variant` | Strategy name currently sent by the app. |
| `symbol` | yes | `XAUUSD` | Market symbol. |
| `higherInterval` | yes | `15m` | Higher strategy interval. |
| `lowerInterval` | yes | `1m` | Lower strategy interval. |
| `startTime` | yes | `1716900000000` | Requested overlay start time in ms. |
| `endTime` | yes | `1716903600000` | Requested overlay end time in ms. |

Response:

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

Top-level event fields:

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `id` | string/number | recommended | Unique event id. Used for de-duplication. |
| `eventType` | string | yes | One of the event types below. |
| `eventTime` | number | yes | Event time in ms. |
| `direction` | string | optional | `LONG` or `SHORT`, used by some overlays. |
| `price` | number | optional | Fallback price for some markers. |
| `payloadJson` | string | yes | JSON string payload. Must parse to an object. |

`payloadJson` is a JSON-encoded string, not a nested object.

## Supported Overlay Event Types

You can implement only candles first. The chart works without overlay events if this endpoint returns `[]`.

### `RANGE_BOUNDARY_UPDATED`

Draws a horizontal range line.

Payload:

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

Fields:

| Field | Description |
| --- | --- |
| `point.side` | Usually `HIGH` or `LOW`. |
| `point.time` | Logical start time. |
| `point.display_time` | Optional display start time. Falls back to `time`. |
| `point.price` | Boundary price. |

### `RANGE_BOUNDARY_ENDED`

Ends a range line created by `RANGE_BOUNDARY_UPDATED`.

Payload:

```json
{
  "side": "HIGH",
  "start_time": 1716900000000,
  "end_time": 1716901800000
}
```

### `RANGE_BOUNDARY_TOUCHED`

Draws a range hit marker.

Payload:

```json
{
  "side": "UPPER"
}
```

`side: "UPPER"` draws an upper break marker. Other values draw a lower break marker.

### `HIGH_N_DETECTED`

Draws an N structure.

Payload:

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

Display time fields are optional; if absent, regular time fields are used.

### `HIGH_N_IN_DETECTED`

Draws base N and signal N-IN structures.

Payload:

```json
{
  "base_n": { "...": "same shape as n" },
  "signal_n": { "...": "same shape as n" }
}
```

### `ENTRY_SIGNAL_OPEN_SENT`

Used for iFVG boxes and position tooltip context.

Payload:

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

iFVG fallback fields:

| Preferred | Fallback |
| --- | --- |
| `entry_signal.fvg.k1.open_time` | `entry_signal.fvg.k1_time`, then `entry_signal.fvg.create_time` |
| `entry_signal.fvg.ifvg_time` | `entry_signal.time` |

### `POSITION_OPEN_FILLED`

Draws a position block and open marker.

Payload:

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

Top-level `direction` should be `LONG` or `SHORT`.

### `POSITION_PARTIAL_CLOSED`

Draws TP1 marker and TP1 line.

Payload:

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

Draws final exit marker and ends the position block.

Payload:

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

The app matches positions by `position_id` when present. If no `position_id` is present, it falls back to `entry_time`.

## Minimal Example Server Behavior

To support only basic K-line viewing:

1. Implement `GET /api/candles`.
2. Optionally implement `/ws/candles`.
3. Return `[]` from `GET /api/strategy-overlay-events`.

That is enough for candles, axes, crosshair, OHLC sketch, pan, and zoom.

