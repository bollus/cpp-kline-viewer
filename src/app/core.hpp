#pragma once

#include <QtWidgets>
#include <QtNetwork>
#include <QtWebSockets/QWebSocket>
#include <QOpenGLWidget>
#if Q4J_HAS_QJS_ENGINE
#include <QJSEngine>
#include <QJSValueIterator>
#endif
#include <QDesktopServices>
#include <QFontDatabase>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

struct Candle {
  qint64 ms = 0;
  double open = 0;
  double high = 0;
  double low = 0;
  double close = 0;
};

struct IndicatorMarker {
  QString indicatorId;
  QString title;
  int index = -1;
  double price = std::numeric_limits<double>::quiet_NaN();
  QColor color = QColor("#409cff");
  QString shape = "circle";
  QString location = "abovebar";
  double pixelOffset = 13.0;
};

struct FvgCircleSettings {
  bool enabled = true;
  int leftRightBars = 1;
  int minGapTicks = 0;
};

struct IndicatorPlot {
  QString indicatorId;
  QString title;
  QVector<double> values;
  QColor color = QColor("#DFD0B8");
  int width = 1;
};

struct IndicatorBox {
  QString indicatorId;
  QString title;
  int from = -1;
  int to = -1;
  double top = std::numeric_limits<double>::quiet_NaN();
  double bottom = std::numeric_limits<double>::quiet_NaN();
  QColor fill = QColor(64, 156, 255, 64);
  QColor border = QColor(147, 197, 253, 210);
};

struct IndicatorLine {
  QString indicatorId;
  QString title;
  int from = -1;
  int to = -1;
  double y1 = std::numeric_limits<double>::quiet_NaN();
  double y2 = std::numeric_limits<double>::quiet_NaN();
  QColor color = QColor("#DFD0B8");
  int width = 1;
};

struct IndicatorLabel {
  QString indicatorId;
  QString text;
  int index = -1;
  double price = std::numeric_limits<double>::quiet_NaN();
  QColor color = QColor("#409cff");
};

struct IndicatorScript {
  QString id;
  QString name;
  QString path;
  bool enabled = true;
};

struct IndicatorInput {
  QString id;
  QString label;
  QString type;
  QVariant value;
  QVariant defaultValue;
  double min = -std::numeric_limits<double>::max();
  double max = std::numeric_limits<double>::max();
};

struct IndicatorRunOutput {
  QString id;
  QString name;
  QVector<IndicatorInput> inputs;
  QVector<IndicatorMarker> markers;
  QVector<IndicatorPlot> plots;
  QVector<IndicatorBox> boxes;
  QVector<IndicatorLine> lines;
  QVector<IndicatorLabel> labels;
  QStringList logs;
  QStringList errors;
};

#if Q4J_HAS_QJS_ENGINE
static QColor parseIndicatorColor(const QJSValue &value, const QColor &fallback) {
  QString text;
  if (value.isString()) text = value.toString().trimmed();
  if (text.isEmpty()) return fallback;
  if (text.startsWith("rgba", Qt::CaseInsensitive)) {
    const int open = text.indexOf('(');
    const int close = text.lastIndexOf(')');
    if (open >= 0 && close > open) {
      const QStringList parts = text.mid(open + 1, close - open - 1).split(',', Qt::SkipEmptyParts);
      if (parts.size() >= 4) {
        const int r = std::clamp(parts[0].trimmed().toInt(), 0, 255);
        const int g = std::clamp(parts[1].trimmed().toInt(), 0, 255);
        const int b = std::clamp(parts[2].trimmed().toInt(), 0, 255);
        const double alpha = parts[3].trimmed().toDouble();
        return QColor(r, g, b, std::clamp(static_cast<int>(std::round(alpha <= 1.0 ? alpha * 255.0 : alpha)), 0, 255));
      }
    }
  }
  const QColor color(text);
  return color.isValid() ? color : fallback;
}

static QString jsStringProperty(const QJSValue &object, const QString &name, const QString &fallback = {}) {
  if (!object.isObject()) return fallback;
  const QJSValue value = object.property(name);
  return value.isUndefined() || value.isNull() ? fallback : value.toString();
}

static int jsIntProperty(const QJSValue &object, const QString &name, int fallback) {
  if (!object.isObject()) return fallback;
  const QJSValue value = object.property(name);
  return value.isNumber() ? value.toInt() : fallback;
}

static double jsDoubleProperty(const QJSValue &object, const QString &name, double fallback) {
  if (!object.isObject()) return fallback;
  const QJSValue value = object.property(name);
  return value.isNumber() ? value.toNumber() : fallback;
}

static QColor jsColorProperty(const QJSValue &object, const QString &name, const QColor &fallback) {
  if (!object.isObject()) return fallback;
  return parseIndicatorColor(object.property(name), fallback);
}

static QVector<double> jsArrayToNumbers(const QJSValue &value) {
  QVector<double> values;
  if (value.isArray()) {
    const int length = value.property("length").toInt();
    values.reserve(length);
    for (int i = 0; i < length; ++i) {
      const QJSValue item = value.property(i);
      values.push_back(item.isNumber() ? item.toNumber() : std::numeric_limits<double>::quiet_NaN());
    }
  } else if (value.isNumber()) {
    values.push_back(value.toNumber());
  }
  return values;
}

static QVector<bool> jsArrayToBools(const QJSValue &value) {
  QVector<bool> values;
  if (value.isArray()) {
    const int length = value.property("length").toInt();
    values.reserve(length);
    for (int i = 0; i < length; ++i) values.push_back(value.property(i).toBool());
  } else if (!value.isUndefined() && !value.isNull()) {
    values.push_back(value.toBool());
  }
  return values;
}

static QJSValue numbersToJsArray(QJSEngine *engine, const QVector<double> &values) {
  QJSValue array = engine->newArray(values.size());
  for (int i = 0; i < values.size(); ++i) {
    if (std::isfinite(values[i])) array.setProperty(i, values[i]);
    else array.setProperty(i, QJSValue(QJSValue::NullValue));
  }
  return array;
}

class IndicatorScriptBridge : public QObject {
  Q_OBJECT

public:
  IndicatorScriptBridge(QJSEngine *engine, IndicatorRunOutput *output, const QVector<Candle> *candles, const QVariantHash *inputValues, QObject *parent = nullptr)
      : QObject(parent), engine_(engine), output_(output), candles_(candles), inputValues_(inputValues) {}

  Q_INVOKABLE void indicator(const QString &name, const QJSValue &) {
    if (!name.trimmed().isEmpty()) output_->name = name.trimmed();
  }

  Q_INVOKABLE int inputInt(int value, const QString &label, int min, int max) {
    const QString id = inputId(label);
    const int resolved = std::clamp(inputValues_ ? inputValues_->value(id, value).toInt() : value, min, max);
    output_->inputs.push_back({id, labelFor(id, label), "int", resolved, value, static_cast<double>(min), static_cast<double>(max)});
    return resolved;
  }

  Q_INVOKABLE double inputFloat(double value, const QString &label, double min, double max) {
    const QString id = inputId(label);
    const double resolved = std::clamp(inputValues_ ? inputValues_->value(id, value).toDouble() : value, min, max);
    output_->inputs.push_back({id, labelFor(id, label), "float", resolved, value, min, max});
    return resolved;
  }

  Q_INVOKABLE bool inputBool(bool value, const QString &label) {
    const QString id = inputId(label);
    const bool resolved = inputValues_ ? inputValues_->value(id, value).toBool() : value;
    output_->inputs.push_back({id, labelFor(id, label), "bool", resolved, value, 0, 1});
    return resolved;
  }

  Q_INVOKABLE void plot(const QJSValue &series, const QJSValue &options) {
    IndicatorPlot plot;
    plot.indicatorId = output_->id;
    plot.title = jsStringProperty(options, "title", output_->name);
    plot.values = jsArrayToNumbers(series);
    plot.color = jsColorProperty(options, "color", plot.color);
    plot.width = std::clamp(jsIntProperty(options, "width", 1), 1, 6);
    if (!plot.values.isEmpty()) output_->plots.push_back(plot);
  }

  Q_INVOKABLE void plotshape(const QJSValue &conditions, const QJSValue &options) {
    const QVector<bool> flags = jsArrayToBools(conditions);
    if (!candles_ || candles_->isEmpty() || flags.isEmpty()) return;
    const QString location = jsStringProperty(options, "location", "abovebar").toLower();
    const QString text = jsStringProperty(options, "text", "");
    QString shape = jsStringProperty(options, "style", jsStringProperty(options, "shape", "circle")).toLower();
    if (shape == "triangledown") shape = "triangle_down";
    if (shape == "triangleup") shape = "triangle_up";
    const QColor color = jsColorProperty(options, "color", QColor("#409cff"));
    const double offset = std::clamp(jsDoubleProperty(options, "offsetPx", jsDoubleProperty(options, "offset", 13.0)), 0.0, 80.0);
    const QVector<double> priceSeries = jsArrayToNumbers(options.property("price"));
    const int count = std::min(static_cast<int>(flags.size()), static_cast<int>(candles_->size()));
    for (int i = 0; i < count; ++i) {
      if (!flags[i]) continue;
      double price = std::numeric_limits<double>::quiet_NaN();
      if (location == "belowbar") price = candles_->at(i).low;
      else if (location == "absolute" && i < priceSeries.size()) price = priceSeries[i];
      else price = candles_->at(i).high;
      output_->markers.push_back({output_->id, text.isEmpty() ? output_->name : text, i, price, color, shape, location, offset});
    }
  }

  Q_INVOKABLE void box(const QJSValue &options) {
    IndicatorBox box;
    box.indicatorId = output_->id;
    box.title = jsStringProperty(options, "title", output_->name);
    box.from = jsIntProperty(options, "from", -1);
    box.to = jsIntProperty(options, "to", -1);
    box.top = jsDoubleProperty(options, "top", std::numeric_limits<double>::quiet_NaN());
    box.bottom = jsDoubleProperty(options, "bottom", std::numeric_limits<double>::quiet_NaN());
    box.fill = jsColorProperty(options, "color", box.fill);
    box.border = jsColorProperty(options, "borderColor", box.border);
    if (box.from >= 0 && box.to >= 0 && std::isfinite(box.top) && std::isfinite(box.bottom)) output_->boxes.push_back(box);
  }

  Q_INVOKABLE void line(const QJSValue &options) {
    IndicatorLine line;
    line.indicatorId = output_->id;
    line.title = jsStringProperty(options, "title", output_->name);
    line.from = jsIntProperty(options, "from", -1);
    line.to = jsIntProperty(options, "to", -1);
    line.y1 = jsDoubleProperty(options, "y1", std::numeric_limits<double>::quiet_NaN());
    line.y2 = jsDoubleProperty(options, "y2", std::numeric_limits<double>::quiet_NaN());
    line.color = jsColorProperty(options, "color", line.color);
    line.width = std::clamp(jsIntProperty(options, "width", 1), 1, 6);
    if (line.from >= 0 && line.to >= 0 && std::isfinite(line.y1) && std::isfinite(line.y2)) output_->lines.push_back(line);
  }

  Q_INVOKABLE void label(const QJSValue &options) {
    IndicatorLabel label;
    label.indicatorId = output_->id;
    label.text = jsStringProperty(options, "text", output_->name);
    label.index = jsIntProperty(options, "index", -1);
    label.price = jsDoubleProperty(options, "price", std::numeric_limits<double>::quiet_NaN());
    label.color = jsColorProperty(options, "color", label.color);
    if (label.index >= 0 && std::isfinite(label.price)) output_->labels.push_back(label);
  }

  Q_INVOKABLE void log(const QJSValue &value) {
    output_->logs.push_back(value.toString());
  }

private:
  QString inputId(const QString &label) {
    QString id = label.trimmed().toLower();
    id.replace(QRegularExpression("[^a-z0-9_\\-]+"), "_");
    while (id.startsWith('_')) id.remove(0, 1);
    while (id.endsWith('_')) id.chop(1);
    if (id.isEmpty()) id = QString("input_%1").arg(inputCounter_ + 1);
    const int count = inputIdCounts_.value(id, 0);
    inputIdCounts_.insert(id, count + 1);
    ++inputCounter_;
    return count <= 0 ? id : QString("%1_%2").arg(id).arg(count + 1);
  }

  QString labelFor(const QString &id, const QString &label) const {
    const QString trimmed = label.trimmed();
    return trimmed.isEmpty() ? id : trimmed;
  }

  QJSEngine *engine_ = nullptr;
  IndicatorRunOutput *output_ = nullptr;
  const QVector<Candle> *candles_ = nullptr;
  const QVariantHash *inputValues_ = nullptr;
  QHash<QString, int> inputIdCounts_;
  int inputCounter_ = 0;
};

class IndicatorTaApi : public QObject {
  Q_OBJECT

public:
  explicit IndicatorTaApi(QJSEngine *engine, QObject *parent = nullptr) : QObject(parent), engine_(engine) {}

  Q_INVOKABLE QJSValue sma(const QJSValue &series, int length) {
    const QVector<double> input = jsArrayToNumbers(series);
    QVector<double> out(input.size(), std::numeric_limits<double>::quiet_NaN());
    if (length <= 0) return numbersToJsArray(engine_, out);
    double sum = 0;
    int valid = 0;
    for (int i = 0; i < input.size(); ++i) {
      if (std::isfinite(input[i])) {
        sum += input[i];
        ++valid;
      }
      if (i >= length && std::isfinite(input[i - length])) {
        sum -= input[i - length];
        --valid;
      }
      if (i >= length - 1 && valid == length) out[i] = sum / length;
    }
    return numbersToJsArray(engine_, out);
  }

  Q_INVOKABLE QJSValue ema(const QJSValue &series, int length) {
    const QVector<double> input = jsArrayToNumbers(series);
    QVector<double> out(input.size(), std::numeric_limits<double>::quiet_NaN());
    if (length <= 0) return numbersToJsArray(engine_, out);
    const double alpha = 2.0 / (length + 1.0);
    double previous = std::numeric_limits<double>::quiet_NaN();
    for (int i = 0; i < input.size(); ++i) {
      if (!std::isfinite(input[i])) continue;
      previous = std::isfinite(previous) ? alpha * input[i] + (1.0 - alpha) * previous : input[i];
      out[i] = previous;
    }
    return numbersToJsArray(engine_, out);
  }

  Q_INVOKABLE QJSValue highest(const QJSValue &series, int length) {
    return rollingExtreme(series, length, true);
  }

  Q_INVOKABLE QJSValue lowest(const QJSValue &series, int length) {
    return rollingExtreme(series, length, false);
  }

  Q_INVOKABLE QJSValue atr(const QJSValue &high, const QJSValue &low, const QJSValue &close, int length) {
    const QVector<double> h = jsArrayToNumbers(high);
    const QVector<double> l = jsArrayToNumbers(low);
    const QVector<double> c = jsArrayToNumbers(close);
    const int count = std::min({static_cast<int>(h.size()), static_cast<int>(l.size()), static_cast<int>(c.size())});
    QVector<double> tr(count, std::numeric_limits<double>::quiet_NaN());
    for (int i = 0; i < count; ++i) {
      if (!std::isfinite(h[i]) || !std::isfinite(l[i])) continue;
      if (i == 0 || !std::isfinite(c[i - 1])) tr[i] = h[i] - l[i];
      else tr[i] = std::max({h[i] - l[i], std::abs(h[i] - c[i - 1]), std::abs(l[i] - c[i - 1])});
    }
    return ema(numbersToJsArray(engine_, tr), length);
  }

  Q_INVOKABLE QJSValue crossover(const QJSValue &left, const QJSValue &right) {
    return cross(left, right, true);
  }

  Q_INVOKABLE QJSValue crossunder(const QJSValue &left, const QJSValue &right) {
    return cross(left, right, false);
  }

private:
  QJSValue rollingExtreme(const QJSValue &series, int length, bool high) {
    const QVector<double> input = jsArrayToNumbers(series);
    QVector<double> out(input.size(), std::numeric_limits<double>::quiet_NaN());
    if (length <= 0) return numbersToJsArray(engine_, out);
    for (int i = length - 1; i < input.size(); ++i) {
      double value = high ? std::numeric_limits<double>::lowest() : std::numeric_limits<double>::max();
      bool ok = true;
      for (int j = i - length + 1; j <= i; ++j) {
        if (!std::isfinite(input[j])) {
          ok = false;
          break;
        }
        value = high ? std::max(value, input[j]) : std::min(value, input[j]);
      }
      if (ok) out[i] = value;
    }
    return numbersToJsArray(engine_, out);
  }

  QJSValue cross(const QJSValue &left, const QJSValue &right, bool over) {
    const QVector<double> a = jsArrayToNumbers(left);
    const QVector<double> b = jsArrayToNumbers(right);
    const int count = std::min(static_cast<int>(a.size()), static_cast<int>(b.size()));
    QJSValue out = engine_->newArray(count);
    for (int i = 1; i < count; ++i) {
      const bool valid = std::isfinite(a[i]) && std::isfinite(b[i]) && std::isfinite(a[i - 1]) && std::isfinite(b[i - 1]);
      const bool crossed = valid && (over ? (a[i - 1] <= b[i - 1] && a[i] > b[i]) : (a[i - 1] >= b[i - 1] && a[i] < b[i]));
      out.setProperty(i, crossed);
    }
    return out;
  }

  QJSEngine *engine_ = nullptr;
};
#endif

class IndicatorEngine {
public:
  IndicatorEngine() {
    reloadScripts();
  }

  void setFvgCircleSettings(const FvgCircleSettings &settings) {
    fvgCircle_ = settings;
    fvgCircle_.leftRightBars = std::clamp(fvgCircle_.leftRightBars, 1, 50);
    fvgCircle_.minGapTicks = std::clamp(fvgCircle_.minGapTicks, 0, 10000);
  }

  const FvgCircleSettings &fvgCircleSettings() const { return fvgCircle_; }
  const QVector<IndicatorMarker> &markers() const { return markers_; }
  const QVector<IndicatorPlot> &plots() const { return plots_; }
  const QVector<IndicatorBox> &boxes() const { return boxes_; }
  const QVector<IndicatorLine> &lines() const { return lines_; }
  const QVector<IndicatorLabel> &labels() const { return labels_; }
  const QVector<IndicatorScript> &scripts() const { return scripts_; }
  const QStringList &errors() const { return errors_; }
  bool scriptRuntimeAvailable() const { return Q4J_HAS_QJS_ENGINE; }
  QVector<IndicatorInput> scriptInputs(const QString &id) const { return inputsByScript_.value(id); }
  QVariantHash scriptInputValues(const QString &id) const { return scriptInputValues_.value(id); }

  QString scriptDirectory() const {
    const QString dir = QCoreApplication::applicationDirPath() + "/indicators";
    QDir().mkpath(dir);
    return dir;
  }

  void setScriptEnabled(const QString &id, bool enabled) {
    scriptEnabledById_[id] = enabled;
    for (IndicatorScript &script : scripts_) {
      if (script.id == id) script.enabled = enabled;
    }
  }

  void setScriptInputValue(const QString &scriptId, const QString &inputId, const QVariant &value) {
    scriptInputValues_[scriptId].insert(inputId, value);
  }

  void reloadScripts() {
    scripts_.clear();
    const QStringList roots{
      QCoreApplication::applicationDirPath() + "/indicators",
      QCoreApplication::applicationDirPath() + "/../indicators",
      QDir::currentPath() + "/cpp-kline-viewer/indicators",
      QDir::currentPath() + "/indicators"
    };
    QSet<QString> seen;
    for (const QString &root : roots) {
      QDir dir(root);
      if (!dir.exists()) continue;
      for (const QString &file : dir.entryList({"*.js"}, QDir::Files, QDir::Name)) {
        const QString path = QFileInfo(dir.filePath(file)).canonicalFilePath();
        if (path.isEmpty() || seen.contains(path)) continue;
        seen.insert(path);
        const QString id = QFileInfo(path).baseName();
        const bool defaultEnabled = !id.startsWith("example-", Qt::CaseInsensitive);
        scripts_.push_back({id, QFileInfo(path).baseName(), path, scriptEnabledById_.value(id, defaultEnabled)});
      }
    }
  }

  void rebuild(const QVector<Candle> &candles) {
    markers_.clear();
    plots_.clear();
    boxes_.clear();
    lines_.clear();
    labels_.clear();
    errors_.clear();
    inputsByScript_.clear();
    if (fvgCircle_.enabled) buildFvgCircle(candles);
#if Q4J_HAS_QJS_ENGINE
    for (const IndicatorScript &script : scripts_) {
      if (script.enabled) runScript(script, candles);
    }
#else
    if (!scripts_.isEmpty()) errors_.push_back("当前构建未启用 QtQml，JS 自定义指标不可用。");
#endif
  }

private:
  static int decimalPlaces(double value) {
    QString text = QString::number(std::abs(value), 'f', 8);
    while (text.contains('.') && text.endsWith('0')) text.chop(1);
    const int dot = text.indexOf('.');
    return dot < 0 ? 0 : text.size() - dot - 1;
  }

  static double estimateTickSize(const QVector<Candle> &candles) {
    int decimals = 0;
    const int size = static_cast<int>(candles.size());
    const int limit = std::min(600, size);
    for (int i = std::max(0, size - limit); i < size; ++i) {
      decimals = std::max(decimals, decimalPlaces(candles[i].open));
      decimals = std::max(decimals, decimalPlaces(candles[i].high));
      decimals = std::max(decimals, decimalPlaces(candles[i].low));
      decimals = std::max(decimals, decimalPlaces(candles[i].close));
    }
    decimals = std::clamp(decimals, 0, 8);
    return std::pow(10.0, -decimals);
  }

  void buildFvgCircle(const QVector<Candle> &candles) {
    const int n = fvgCircle_.leftRightBars;
    if (candles.size() < 2 * n + 1) return;
    const double minGap = fvgCircle_.minGapTicks * estimateTickSize(candles);
    for (int current = 2 * n; current < candles.size(); ++current) {
      double rightLow = std::numeric_limits<double>::max();
      double rightHigh = std::numeric_limits<double>::lowest();
      for (int i = current - n + 1; i <= current; ++i) {
        rightLow = std::min(rightLow, candles[i].low);
        rightHigh = std::max(rightHigh, candles[i].high);
      }

      double leftHigh = std::numeric_limits<double>::lowest();
      double leftLow = std::numeric_limits<double>::max();
      for (int i = current - 2 * n; i <= current - n - 1; ++i) {
        leftHigh = std::max(leftHigh, candles[i].high);
        leftLow = std::min(leftLow, candles[i].low);
      }

      const bool bullFvg = (leftHigh + minGap) < rightLow;
      const bool bearFvg = (leftLow - minGap) > rightHigh;
      if (!bullFvg && !bearFvg) continue;

      const int center = current - n;
      markers_.push_back({
        "builtin.fvg-circle",
        "FVG Circle",
        center,
        candles[center].high,
        QColor("#409cff"),
        "circle",
        "abovebar",
        13.0
      });
    }
  }

#if Q4J_HAS_QJS_ENGINE
  static QJSValue candleSeries(QJSEngine &engine, const QVector<Candle> &candles, const std::function<double(const Candle &, int)> &reader) {
    QJSValue array = engine.newArray(candles.size());
    for (int i = 0; i < candles.size(); ++i) array.setProperty(i, reader(candles[i], i));
    return array;
  }

  void runScript(const IndicatorScript &script, const QVector<Candle> &candles) {
    QFile file(script.path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      errors_.push_back(QString("%1: 无法读取脚本").arg(script.name));
      return;
    }

    QJSEngine engine;
    IndicatorRunOutput output;
    output.id = script.id;
    output.name = script.name;
    const QVariantHash inputValues = scriptInputValues_.value(script.id);
    IndicatorScriptBridge bridge(&engine, &output, &candles, &inputValues);
    IndicatorTaApi ta(&engine);
    engine.globalObject().setProperty("__api", engine.newQObject(&bridge));
    engine.globalObject().setProperty("ta", engine.newQObject(&ta));
    engine.globalObject().setProperty("open", candleSeries(engine, candles, [](const Candle &c, int) { return c.open; }));
    engine.globalObject().setProperty("high", candleSeries(engine, candles, [](const Candle &c, int) { return c.high; }));
    engine.globalObject().setProperty("low", candleSeries(engine, candles, [](const Candle &c, int) { return c.low; }));
    engine.globalObject().setProperty("close", candleSeries(engine, candles, [](const Candle &c, int) { return c.close; }));
    engine.globalObject().setProperty("time", candleSeries(engine, candles, [](const Candle &c, int) { return static_cast<double>(c.ms); }));
    engine.globalObject().setProperty("bar_index", candleSeries(engine, candles, [](const Candle &, int i) { return static_cast<double>(i); }));
    engine.globalObject().setProperty("last_bar_index", QJSValue(candles.isEmpty() ? -1 : static_cast<int>(candles.size()) - 1));
    const QString prelude = R"JS(
      var input = {
        int: (value, label = "", min = -2147483648, max = 2147483647) => __api.inputInt(value, label, min, max),
        float: (value, label = "", min = -Number.MAX_VALUE, max = Number.MAX_VALUE) => __api.inputFloat(value, label, min, max),
        bool: (value, label = "") => __api.inputBool(value, label)
      };
      var location = { abovebar: "abovebar", belowbar: "belowbar", absolute: "absolute" };
      var indicator = (name, options = {}) => __api.indicator(name, options);
      var plot = (series, options = {}) => __api.plot(series, options);
      var plotshape = (conditions, options = {}) => __api.plotshape(conditions, options);
      var box = (options = {}) => __api.box(options);
      var line = (options = {}) => __api.line(options);
      var label = (options = {}) => __api.label(options);
      var ref = (series, barsBack = 1) => {
        const out = new Array(series.length).fill(null);
        for (let i = barsBack; i < series.length; i++) out[i] = series[i - barsBack];
        return out;
      };
      var nz = (series, value = 0) => {
        if (Array.isArray(series)) return series.map(v => Number.isFinite(v) ? v : value);
        return Number.isFinite(series) ? series : value;
      };
      var console = { log: (value) => __api.log(String(value)) };
    )JS";
    QJSValue preludeResult = engine.evaluate(prelude, "indicator-runtime.js");
    if (preludeResult.isError()) {
      errors_.push_back(QString("%1: runtime 初始化失败: %2").arg(script.name, preludeResult.toString()));
      return;
    }
    QJSValue result = engine.evaluate(QString::fromUtf8(file.readAll()), script.path);
    if (result.isError()) {
      errors_.push_back(QString("%1:%2 %3").arg(script.name).arg(result.property("lineNumber").toInt()).arg(result.toString()));
      return;
    }

    markers_ += output.markers;
    plots_ += output.plots;
    boxes_ += output.boxes;
    lines_ += output.lines;
    labels_ += output.labels;
    errors_ += output.errors;
    inputsByScript_.insert(script.id, output.inputs);
  }
#endif

  FvgCircleSettings fvgCircle_;
  QVector<IndicatorScript> scripts_;
  QHash<QString, bool> scriptEnabledById_;
  QHash<QString, QVariantHash> scriptInputValues_;
  QHash<QString, QVector<IndicatorInput>> inputsByScript_;
  QVector<IndicatorMarker> markers_;
  QVector<IndicatorPlot> plots_;
  QVector<IndicatorBox> boxes_;
  QVector<IndicatorLine> lines_;
  QVector<IndicatorLabel> labels_;
  QStringList errors_;
};

class CodeEditor;

class JsSyntaxHighlighter : public QSyntaxHighlighter {
public:
  explicit JsSyntaxHighlighter(QTextDocument *parent = nullptr) : QSyntaxHighlighter(parent) {
    auto format = [](const QColor &color, QFont::Weight weight = QFont::Normal, bool italic = false) {
      QTextCharFormat f;
      f.setForeground(color);
      f.setFontWeight(weight);
      f.setFontItalic(italic);
      return f;
    };

    keywordFormat_ = format(QColor("#c792ea"), QFont::DemiBold);
    apiFormat_ = format(QColor("#82aaff"), QFont::DemiBold);
    stringFormat_ = format(QColor("#c3e88d"));
    numberFormat_ = format(QColor("#f78c6c"));
    commentFormat_ = format(QColor("#7c8796"), QFont::Normal, true);

    const QStringList keywords{
      "const", "let", "var", "function", "return", "if", "else", "for", "while",
      "break", "continue", "true", "false", "null", "NaN", "Infinity", "new"
    };
    for (const QString &keyword : keywords) rules_.push_back({QRegularExpression(QString("\\b%1\\b").arg(keyword)), keywordFormat_});

    const QStringList apis{
      "indicator", "input", "plot", "plotshape", "box", "line", "label", "ta",
      "open", "high", "low", "close", "time", "bar_index", "last_bar_index",
      "location", "ref", "nz", "Math", "Number"
    };
    for (const QString &api : apis) rules_.push_back({QRegularExpression(QString("\\b%1\\b").arg(api)), apiFormat_});

    rules_.push_back({QRegularExpression("\\b\\d+(?:\\.\\d+)?\\b"), numberFormat_});
    rules_.push_back({QRegularExpression("\"[^\"\\\\]*(?:\\\\.[^\"\\\\]*)*\""), stringFormat_});
    rules_.push_back({QRegularExpression("'[^'\\\\]*(?:\\\\.[^'\\\\]*)*'"), stringFormat_});
    rules_.push_back({QRegularExpression("`[^`\\\\]*(?:\\\\.[^`\\\\]*)*`"), stringFormat_});
    rules_.push_back({QRegularExpression("//[^\\n]*"), commentFormat_});
  }

protected:
  void highlightBlock(const QString &text) override {
    for (const Rule &rule : rules_) {
      QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
      while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        setFormat(match.capturedStart(), match.capturedLength(), rule.format);
      }
    }

    setCurrentBlockState(0);
    int start = previousBlockState() == 1 ? 0 : text.indexOf("/*");
    while (start >= 0) {
      const int end = text.indexOf("*/", start + 2);
      const int length = end < 0 ? text.length() - start : end - start + 2;
      setFormat(start, length, commentFormat_);
      if (end < 0) {
        setCurrentBlockState(1);
        break;
      }
      start = text.indexOf("/*", start + length);
    }
  }

private:
  struct Rule {
    QRegularExpression pattern;
    QTextCharFormat format;
  };

  QVector<Rule> rules_;
  QTextCharFormat keywordFormat_;
  QTextCharFormat apiFormat_;
  QTextCharFormat stringFormat_;
  QTextCharFormat numberFormat_;
  QTextCharFormat commentFormat_;
};

class LineNumberArea : public QWidget {
public:
  explicit LineNumberArea(CodeEditor *editor);
  QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  CodeEditor *editor_ = nullptr;
};

class CodeEditor : public QPlainTextEdit {
public:
  explicit CodeEditor(QWidget *parent = nullptr) : QPlainTextEdit(parent) {
    lineNumberArea_ = new LineNumberArea(this);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    updateLineNumberAreaWidth();
  }

  void applyComfortableLineSpacing() {
    QTextCursor cursor(document());
    cursor.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setLineHeight(132, QTextBlockFormat::ProportionalHeight);
    cursor.mergeBlockFormat(format);
  }

  int lineNumberAreaWidth() const {
    int digits = 1;
    int max = std::max(1, blockCount());
    while (max >= 10) {
      max /= 10;
      ++digits;
    }
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
  }

  void lineNumberAreaPaintEvent(QPaintEvent *event) {
    QPainter painter(lineNumberArea_);
    painter.fillRect(event->rect(), palette().color(QPalette::Window).darker(108));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    painter.setFont(font());
    painter.setPen(palette().color(QPalette::PlaceholderText));
    while (block.isValid() && top <= event->rect().bottom()) {
      if (block.isVisible() && bottom >= event->rect().top()) {
        painter.drawText(0, top, lineNumberArea_->width() - 6, fontMetrics().height(), Qt::AlignRight, QString::number(blockNumber + 1));
      }
      block = block.next();
      top = bottom;
      bottom = top + qRound(blockBoundingRect(block).height());
      ++blockNumber;
    }
  }

protected:
  void resizeEvent(QResizeEvent *event) override {
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    lineNumberArea_->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
  }

private:
  void updateLineNumberAreaWidth() {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
  }

  void updateLineNumberArea(const QRect &rect, int dy) {
    if (dy) lineNumberArea_->scroll(0, dy);
    else lineNumberArea_->update(0, rect.y(), lineNumberArea_->width(), rect.height());
    if (rect.contains(viewport()->rect())) updateLineNumberAreaWidth();
  }

  QWidget *lineNumberArea_ = nullptr;
};

inline LineNumberArea::LineNumberArea(CodeEditor *editor) : QWidget(editor), editor_(editor) {}

inline QSize LineNumberArea::sizeHint() const {
  return QSize(editor_ ? editor_->lineNumberAreaWidth() : 0, 0);
}

inline void LineNumberArea::paintEvent(QPaintEvent *event) {
  if (editor_) editor_->lineNumberAreaPaintEvent(event);
}

static QString envOrDefault(const char *name, const QString &fallback) {
  const QByteArray value = qgetenv(name);
  return value.isEmpty() ? fallback : QString::fromUtf8(value);
}

static QString normalizeBase(QString value) {
  while (value.endsWith('/')) value.chop(1);
  return value;
}

static QString wsFromHttp(QString value) {
  if (value.startsWith("https://")) return "wss://" + value.mid(8);
  if (value.startsWith("http://")) return "ws://" + value.mid(7);
  return value;
}

static qint64 intervalMs(const QString &interval) {
  static const QHash<QString, qint64> values{
    {"1m", 60000}, {"2m", 120000}, {"3m", 180000}, {"4m", 240000},
    {"5m", 300000}, {"10m", 600000}, {"15m", 900000}, {"30m", 1800000},
    {"1h", 3600000}, {"4h", 14400000}, {"12h", 43200000},
    {"1d", 86400000}, {"1w", 604800000}
  };
  return values.value(interval, 60000);
}

static QVector<int> versionParts(QString value) {
  value = value.trimmed();
  if (value.startsWith('v', Qt::CaseInsensitive)) value.remove(0, 1);
  QVector<int> parts;
  for (const QString &part : value.split('.')) {
    QString digits;
    for (const QChar ch : part) {
      if (ch.isDigit()) digits.append(ch);
      else break;
    }
    parts.push_back(digits.isEmpty() ? 0 : digits.toInt());
  }
  while (parts.size() < 3) parts.push_back(0);
  return parts;
}

static int compareVersions(const QString &left, const QString &right) {
  const QVector<int> a = versionParts(left);
  const QVector<int> b = versionParts(right);
  const int count = std::max(a.size(), b.size());
  for (int i = 0; i < count; ++i) {
    const int av = i < a.size() ? a[i] : 0;
    const int bv = i < b.size() ? b[i] : 0;
    if (av != bv) return av < bv ? -1 : 1;
  }
  return 0;
}

static QString loadBundledFonts() {
  const QStringList roots{
    QCoreApplication::applicationDirPath() + "/fonts",
    QCoreApplication::applicationDirPath() + "/../fonts",
    QDir::currentPath() + "/cpp-kline-viewer/fonts",
    QDir::currentPath() + "/fonts"
  };
  QString preferredFamily;
  for (const QString &root : roots) {
    QDir dir(root);
    if (!dir.exists()) continue;
    QDir staticDir(dir.filePath("static"));
    if (staticDir.exists()) {
      for (const QString &file : staticDir.entryList({"*.ttf", "*.otf"}, QDir::Files)) {
        const int id = QFontDatabase::addApplicationFont(staticDir.filePath(file));
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        if (preferredFamily.isEmpty() && !families.isEmpty()) preferredFamily = families.first();
      }
    }
    for (const QString &file : dir.entryList({"*.ttf", "*.otf"}, QDir::Files)) {
      const int id = QFontDatabase::addApplicationFont(dir.filePath(file));
      const QStringList families = QFontDatabase::applicationFontFamilies(id);
      if (preferredFamily.isEmpty() && !families.isEmpty()) preferredFamily = families.first();
    }
  }
  return preferredFamily.isEmpty() ? "Chiron GoRound TC" : preferredFamily;
}

static QString systemUiFontFamily() {
#ifdef Q_OS_WIN
  return "Microsoft YaHei UI";
#elif defined(Q_OS_MACOS)
  return "PingFang SC";
#else
  return "Noto Sans CJK SC";
#endif
}
