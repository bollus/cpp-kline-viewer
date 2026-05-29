indicator("Example MA + FVG", { overlay: true })

const fastLength = input.int(9, "Fast EMA", 1, 200)
const slowLength = input.int(21, "Slow EMA", 1, 300)
const n = input.int(1, "FVG Left/Right N", 1, 50)

const fast = ta.ema(close, fastLength)
const slow = ta.ema(close, slowLength)

plot(fast, { title: "EMA Fast", color: "#20c997", width: 1 })
plot(slow, { title: "EMA Slow", color: "#f0b64f", width: 1 })

plotshape(ta.crossover(fast, slow), {
  location: location.belowbar,
  text: "BUY",
  color: "#20c997"
})

plotshape(ta.crossunder(fast, slow), {
  location: location.abovebar,
  text: "SELL",
  color: "#ef5f78"
})

for (let i = 2 * n; i < close.length; i++) {
  let rightLow = Infinity
  let rightHigh = -Infinity
  for (let j = i - n + 1; j <= i; j++) {
    rightLow = Math.min(rightLow, low[j])
    rightHigh = Math.max(rightHigh, high[j])
  }

  let leftHigh = -Infinity
  let leftLow = Infinity
  for (let j = i - 2 * n; j <= i - n - 1; j++) {
    leftHigh = Math.max(leftHigh, high[j])
    leftLow = Math.min(leftLow, low[j])
  }

  const center = i - n
  if (leftHigh < rightLow || leftLow > rightHigh) {
    label({
      index: center,
      price: high[center],
      text: "FVG",
      color: "#409cff"
    })
  }
}
