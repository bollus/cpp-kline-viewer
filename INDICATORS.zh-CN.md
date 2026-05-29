# 自定义指标脚本

程序支持将 JavaScript 指标文件放到运行目录的 `indicators` 文件夹中。打开“策略设置”后，可以重载脚本、打开目录、启用或禁用每个指标。

> JS 指标运行时依赖 QtQml / QJSEngine。GitHub Actions 构建已安装 `qtdeclarative`，会启用该能力。

## 软件内编辑

打开顶部的“指标”按钮，可以直接管理自定义指标：

- `新建指标`：打开代码编辑器，保存后生成新的 `.js` 指标文件
- `复制内置 FVG`：把内置 FVG Circle 生成一份可修改的 JS 指标
- 每个 JS 指标右侧有 `编辑`、`复制`、`删除`
- 编辑器会显示行号，`Ctrl+S` 可以保存
- 文件名会根据 `indicator("指标名称")` 自动生成，特殊符号会被过滤
- 保存代码后会自动重载指标
- 也可以用 `打开目录` 直接管理 `indicators/` 文件夹

## 基础结构

```js
indicator("My Indicator", { overlay: true })

const fast = ta.ema(close, 9)
const slow = ta.ema(close, 21)

plot(fast, { title: "EMA 9", color: "#20c997", width: 1 })
plot(slow, { title: "EMA 21", color: "#f0b64f", width: 1 })

plotshape(ta.crossover(fast, slow), {
  location: location.belowbar,
  text: "BUY",
  color: "#20c997"
})
```

## 可用数据

这些变量都是数组，长度与当前已加载 K 线数量一致：

```js
open
high
low
close
time
bar_index
last_bar_index
```

历史引用可以用：

```js
const prevClose = ref(close, 1)
```

## 参数

```js
const length = input.int(20, "Length", 1, 500)
const ratio = input.float(1.5, "Ratio", 0.1, 10)
const enabled = input.bool(true, "Enabled")
```

程序会在“策略设置”里根据 `input.*` 自动生成参数控件。参数会保存到本机配置中，修改后会立即重算指标。

参数使用 `label` 生成本地保存 key。为了避免保存值混乱，同一个脚本里不要让多个输入使用完全相同的 label。

## TA 函数

```js
ta.sma(series, length)
ta.ema(series, length)
ta.atr(high, low, close, length)
ta.highest(series, length)
ta.lowest(series, length)
ta.crossover(left, right)
ta.crossunder(left, right)
```

## 绘图

### plot

```js
plot(series, {
  title: "Line",
  color: "#409cff",
  width: 2
})
```

### plotshape

```js
plotshape(conditionArray, {
  location: location.abovebar, // abovebar / belowbar / absolute
  text: "Signal",
  color: "#409cff"
})
```

### box

```js
box({
  from: 100,
  to: 120,
  top: 4520.5,
  bottom: 4512.0,
  color: "rgba(64, 156, 255, 0.25)",
  borderColor: "#93c5fd"
})
```

### line

```js
line({
  from: 100,
  to: 150,
  y1: 4500,
  y2: 4520,
  color: "#f0b64f",
  width: 1
})
```

### label

```js
label({
  index: 120,
  price: 4510,
  text: "FVG",
  color: "#409cff"
})
```

## 错误处理

脚本语法错误或运行错误会显示在“策略设置”的“脚本错误”区域，通常包含脚本名、行号和错误内容。

## 性能说明

历史加载、参数修改和指标开关会立即重算指标。实时 K 线更新会做短节流，连续推送会合并重算，避免高频 WebSocket 消息让 UI 明显卡顿。
