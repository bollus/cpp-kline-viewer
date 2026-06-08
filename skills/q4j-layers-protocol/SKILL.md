---
name: q4j-layers-protocol
description: Create, convert, validate, or document Q4J K-Line Viewer strategy overlay layers JSON. Use when working on /api/strategy-overlay-events responses, trading strategy visualization overlays, layers protocol objects, line/box/position/label/marker rendering data, or migrating old eventType/payloadJson strategy events into the common layers schema.
---

# Q4J Layers Protocol

Use this skill to produce or review overlay data that Q4J K-Line Viewer can render directly.

## Workflow

1. Determine whether the task is to create new layers JSON, convert old strategy events, review an API response, or write backend guidance.
2. Read `references/layers-protocol.zh-CN.md` whenever exact fields, examples, or supported layer types are needed.
3. Prefer the common response object:

```json
{
  "version": 2,
  "strategy": "strategy_name",
  "symbol": "XAUUSD",
  "interval": "1m",
  "layers": []
}
```

4. Generate stable `id` values for every layer. Do not use random IDs for deterministic strategy output.
5. Group related shapes with `group` so the chart can expose dynamic layer visibility toggles.
6. Use millisecond timestamps for all time fields and numeric values for prices.

## Protocol Version Notes

### v2

- `position.pnl` was renamed to `position.totalR`.
- `position.totalR` is the weighted total R after one or more exits. Calculate it with each exit's closed-position ratio, for example `1R * 0.5 + 3R * 0.5 = 2R`; do not directly sum each exit R as `1R + 3R`.
- `position.tp1` and `position.tp2` were replaced by `position.tps`, an array that supports one TP or more than two TPs.
- `position.remark` was added for strategy explanations on trade blocks. Migrate older `background` or `signal` explanation fields into `remark`.
- Long remarks are previewed in the hover panel; put the full text or structured explanation in `position.data` when it must be copied exactly.
- The viewer still reads legacy `pnl/tp1/tp2` as a compatibility fallback, but new strategies should output `totalR/tps`.

## Layer Selection

- Use `line` or `polyline` for N structures, trend legs, and connected swing paths.
- Use `box` for FVG, IFVG, supply/demand zones, or any bounded rectangle.
- Use `range` or `priceLine` for horizontal range boundaries and price levels.
- Use `position` for trade blocks with entry, SL, adaptive TPs, totalR, remark, profit/loss regions, hover, and right-click copy.
- Use `label` for text badges anchored to a time/price.
- Use `marker` for compact symbols such as breakouts, touches, entries, exits, and fractals.

## Review Checklist

- Response root is an object with `layers`, not only an old event array, unless legacy compatibility is explicitly required.
- Each layer has `id`, `type`, and enough geometry fields for that type.
- `group` names are human-readable and stable.
- `line`, `polyline`, and `ray` IDs are stable because hover/click highlight persistence is keyed by `id`.
- Dynamic layer visibility is driven by API `group` names; do not rely on client-side fixed strategy layer toggles.
- `zIndex` is used when visual order matters.
- `position.tps` is supplied as an array, even when there is only one TP.
- `position.totalR` is supplied when weighted trade result in R is known.
- `position.remark` replaces older background/signal explanation fields; long details should also be copied into `position.data`.
- `position.data` contains the business payload users expect to copy; otherwise the viewer will synthesize copy JSON.
- Large responses are scoped to the requested visible `startTime/endTime` window.

## Reference

Load `references/layers-protocol.zh-CN.md` for the full schema, supported fields, style options, and examples.
