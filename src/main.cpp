#include <QtWidgets>
#include <QtNetwork>
#include <QtWebSockets/QWebSocket>
#include <QOpenGLWidget>
#if Q4J_HAS_QJS_ENGINE
#include <QJSEngine>
#include <QJSValueIterator>
#endif
#include <QFontDatabase>
#include <QDesktopServices>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#ifndef Q4J_APP_VERSION
#define Q4J_APP_VERSION "1.0.0"
#endif

#ifndef Q4J_UPDATE_REPO
#define Q4J_UPDATE_REPO ""
#endif

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
  QColor color = QColor("#f0b64f");
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
  QColor color = QColor("#f0b64f");
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

LineNumberArea::LineNumberArea(CodeEditor *editor) : QWidget(editor), editor_(editor) {}

QSize LineNumberArea::sizeHint() const {
  return QSize(editor_ ? editor_->lineNumberAreaWidth() : 0, 0);
}

void LineNumberArea::paintEvent(QPaintEvent *event) {
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

class ChartWidget : public QOpenGLWidget {
  Q_OBJECT

public:
  enum class AnnotationTool {
    None,
    LongBlock,
    ShortBlock,
    SegmentLine,
    HorizontalLine,
    VerticalLine,
    Polyline,
    Rectangle
  };

  explicit ChartWidget(QWidget *parent = nullptr) : QOpenGLWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    buildAnnotationStyleToolbar();
  }

  void setDark(bool value) {
    dark_ = value;
    syncAnnotationStyleToolbar();
    update();
  }

  void setTimeZoneId(const QByteArray &id) {
    const QTimeZone zone(id);
    timeZoneId_ = zone.isValid() ? id : QTimeZone::systemTimeZoneId();
    update();
  }

  void setCandles(QVector<Candle> candles) {
    setCandlesInternal(std::move(candles), true);
  }

  void setReplayCandles(QVector<Candle> candles) {
    setCandlesInternal(std::move(candles), false);
  }

  void prependCandles(QVector<Candle> older) {
    if (older.isEmpty()) return;
    const qint64 previousFirst = candles_.isEmpty() ? 0 : candles_.first().ms;
    candles_ += older;
    std::sort(candles_.begin(), candles_.end(), [](const Candle &a, const Candle &b) {
      return a.ms < b.ms;
    });
    candles_.erase(std::unique(candles_.begin(), candles_.end(), [](const Candle &a, const Candle &b) {
      return a.ms == b.ms;
    }), candles_.end());
    const int previousIndex = indexAtTime(previousFirst);
    if (previousIndex > 0) visibleStart_ += previousIndex;
    visibleStart_ = std::clamp(visibleStart_, 0.0, maxVisibleStart());
    rebuildIndicatorsNow();
    emitVisibleRange();
    update();
  }

private:
  void setCandlesInternal(QVector<Candle> candles, bool clearOverlay) {
    candles_ = std::move(candles);
    if (!candles_.isEmpty()) messageText_.clear();
    if (clearOverlay) {
      overlayEvents_ = {};
      parsedOverlayEvents_.clear();
      parsedLayers_.clear();
      layerGroupOrder_.clear();
      layerGroupVisible_.clear();
    }
    std::sort(candles_.begin(), candles_.end(), [](const Candle &a, const Candle &b) {
      return a.ms < b.ms;
    });
    if (visibleCount_ <= 0) visibleCount_ = std::min(160, candleCount());
    visibleStart_ = maxVisibleStart();
    hoveredIndex_ = -1;
    rebuildIndicatorsNow();
    emitVisibleRange();
    update();
  }

public:
  void setOverlayEvents(const QJsonValue &value) {
    overlayEvents_ = {};
    parsedOverlayEvents_.clear();
    parsedLayers_.clear();
    layerGroupOrder_.clear();
    rangeEndByKey_.clear();
    if (value.isObject() && value.toObject().value("layers").isArray()) {
      parseOverlayLayers(value.toObject().value("layers").toArray());
      update();
      return;
    }
    const QJsonArray events = value.isArray() ? value.toArray() : QJsonArray{};
    overlayEvents_ = events;
    for (const QJsonValue &value : overlayEvents_) {
      const QJsonObject event = value.toObject();
      parsedOverlayEvents_.push_back({event, parsePayload(event)});
    }
    std::sort(parsedOverlayEvents_.begin(), parsedOverlayEvents_.end(), [this](const ParsedOverlayEvent &a, const ParsedOverlayEvent &b) {
      return jsonMs(a.event.value("eventTime")) < jsonMs(b.event.value("eventTime"));
    });
    for (const ParsedOverlayEvent &parsed : parsedOverlayEvents_) {
      if (parsed.event.value("eventType").toString() != "RANGE_BOUNDARY_ENDED") continue;
      const QJsonObject payload = parsed.payload;
      const QString key = rangeKey(payload.value("side").toString(), jsonMs(payload.value("start_time")));
      const qint64 end = jsonMs(payload.value("end_time"));
      if (key.isEmpty() || end <= 0) continue;
      const qint64 previous = rangeEndByKey_.value(key, 0);
      if (previous <= 0 || end < previous) rangeEndByKey_.insert(key, end);
    }
    update();
  }

  void showMessage(const QString &message) {
    messageText_ = message;
    update();
  }

  void clearMessage() {
    if (messageText_.isEmpty()) return;
    messageText_.clear();
    update();
  }

  void showLoadError(const QString &message) {
    candles_.clear();
    overlayEvents_ = {};
    parsedOverlayEvents_.clear();
    parsedLayers_.clear();
    layerGroupOrder_.clear();
    rebuildIndicatorsNow();
    hoveredIndex_ = -1;
    hoveredPositionIndex_ = -1;
    messageText_ = message;
    emit visibleRangeChanged(0, 0, -1, -1);
    update();
  }

  void upsertCandle(const Candle &candle) {
    const bool followLatest = isAtRealtimeEdge();
    const auto it = std::lower_bound(candles_.begin(), candles_.end(), candle.ms, [](const Candle &item, qint64 ms) {
      return item.ms < ms;
    });
    if (it != candles_.end() && it->ms == candle.ms) {
      *it = candle;
    } else {
      candles_.insert(it, candle);
    }
    if (followLatest) visibleStart_ = maxVisibleStart();
    scheduleIndicatorRebuild();
    emitVisibleRange();
    scheduleRepaint();
  }

  void setFvgCircleSettings(bool enabled, int leftRightBars, int minGapTicks) {
    indicatorEngine_.setFvgCircleSettings({enabled, leftRightBars, minGapTicks});
    rebuildIndicatorsNow();
    emit fvgCircleVisibilityChanged(enabled);
    update();
  }

  FvgCircleSettings fvgCircleSettings() const {
    return indicatorEngine_.fvgCircleSettings();
  }

  QVector<IndicatorScript> customIndicators() const {
    return indicatorEngine_.scripts();
  }

  QVector<IndicatorInput> customIndicatorInputs(const QString &id) const {
    return indicatorEngine_.scriptInputs(id);
  }

  QVariantHash customIndicatorInputValues(const QString &id) const {
    return indicatorEngine_.scriptInputValues(id);
  }

  QString indicatorScriptDirectory() const {
    return indicatorEngine_.scriptDirectory();
  }

  QStringList indicatorErrors() const {
    return indicatorEngine_.errors();
  }

  bool indicatorScriptRuntimeAvailable() const {
    return indicatorEngine_.scriptRuntimeAvailable();
  }

  void reloadCustomIndicators() {
    indicatorEngine_.reloadScripts();
    rebuildIndicatorsNow();
    update();
  }

  void setCustomIndicatorEnabled(const QString &id, bool enabled) {
    indicatorEngine_.setScriptEnabled(id, enabled);
    rebuildIndicatorsNow();
    update();
  }

  void setCustomIndicatorInputValue(const QString &scriptId, const QString &inputId, const QVariant &value) {
    indicatorEngine_.setScriptInputValue(scriptId, inputId, value);
    rebuildIndicatorsNow();
    update();
  }

  void setLayersVisible(bool range, bool n, bool nin, bool ifvg, bool order, bool marker) {
    rangeVisible_ = range;
    nVisible_ = n;
    ninVisible_ = nin;
    ifvgVisible_ = ifvg;
    orderVisible_ = order;
    markerVisible_ = marker;
    update();
  }

  void setAnnotationTool(AnnotationTool tool) {
    if (annotationTool_ == tool) return;
    annotationTool_ = tool;
    drawingAnnotation_ = false;
    draftPolyline_.clear();
    updateCursor();
    emit annotationToolChanged(tool);
    update();
  }

  void resetAnnotationTool() {
    setAnnotationTool(AnnotationTool::None);
  }

  void setMagnetEnabled(bool enabled) {
    magnetEnabled_ = enabled;
    update();
  }

signals:
  void hoveredCandleChanged(const Candle *candle);
  void olderCandlesRequested(qint64 beforeMs);
  void overlayRangeChanged(qint64 startMs, qint64 endMs);
  void visibleRangeChanged(qint64 startMs, qint64 endMs, int firstIndex, int lastIndex);
  void annotationToolChanged(AnnotationTool tool);
  void fvgCircleVisibilityChanged(bool enabled);
  void customIndicatorVisibilityChanged(const QString &id, bool enabled);

protected:
  bool showPositionContextMenu(const QPointF &pos, const QPoint &globalPos);

  void keyPressEvent(QKeyEvent *event) override {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
      deleteSelectedAnnotation();
      event->accept();
      return;
    }
    if (event->matches(QKeySequence::Undo)) {
      undoAnnotation();
      event->accept();
      return;
    }
    QOpenGLWidget::keyPressEvent(event);
  }

  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    paintBackground(p);
    if (candles_.isEmpty() && !messageText_.isEmpty()) {
      paintChartMessage(p);
      return;
    }
    double minPrice, maxPrice;
    visibleRange(minPrice, maxPrice);
    paintGrid(p, minPrice, maxPrice);
    paintCandles(p);
    paintOverlays(p, minPrice, maxPrice);
    paintIndicators(p, minPrice, maxPrice);
    paintManualAnnotations(p, minPrice, maxPrice);
    paintLatestPriceLine(p, minPrice, maxPrice);
    paintLayerHints(p);
    paintCrosshair(p);
    paintPositionTooltip(p);
    paintOhlcSketch(p);
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    mousePos_ = cursorPosition(event->position());
    hasMouse_ = true;
    if (annotationDragMode_ != AnnotationDragMode::None) {
      updateAnnotationDrag(event->position());
      scheduleRepaint();
      return;
    }
    if (drawingAnnotation_) {
      draftPoint_ = chartPointFromPosition(event->position());
      scheduleRepaint();
      return;
    }
    if (dragging_) {
      const int dx = event->position().x() - dragStart_.x();
      const int dy = event->position().y() - dragStart_.y();
      const double deltaBars = -dx / std::max(0.05, barStep());
      visibleStart_ = std::clamp(dragVisibleStart_ + deltaBars, 0.0, maxVisibleStart());
      manualPriceOffset_ = dragPriceOffset_ + dy * dragPriceRange_ / std::max(1.0, plotRect().height());
      requestMoreIfNeeded();
      emitOverlayRange();
      emitVisibleRange();
      scheduleRepaint();
      return;
    }
    if (xAxisScaling_) {
      const int dx = event->position().x() - dragStart_.x();
      visibleCount_ = std::clamp(static_cast<int>(std::round(axisVisibleCount_ * std::exp(dx / 480.0))), 20, std::max(40, candleCount() + rightOffsetBars_));
      const double newStep = plotRect().width() / std::max(1, visibleCount_);
      const double newLocalIndex = axisAnchorLocalX_ / std::max(0.05, newStep) - 0.5;
      visibleStart_ = std::clamp(axisAnchorIndex_ - newLocalIndex, 0.0, maxVisibleStart());
      emitOverlayRange();
      emitVisibleRange();
      scheduleRepaint();
      return;
    }
    if (yAxisScaling_) {
      const int dy = event->position().y() - dragStart_.y();
      manualPriceScale_ = std::clamp(axisPriceScale_ * std::exp(dy / 180.0), 0.2, 8.0);
      scheduleRepaint();
      return;
    }
    hoveredIndex_ = indexAt(mousePos_.x());
    hoveredPositionIndex_ = positionHitboxAt(event->position());
    if (hoveredIndex_ >= 0 && hoveredIndex_ < candleCount()) {
      emit hoveredCandleChanged(&candles_[hoveredIndex_]);
    } else {
      emit hoveredCandleChanged(nullptr);
    }
    update();
  }

  void leaveEvent(QEvent *) override {
    hoveredIndex_ = -1;
    hoveredPositionIndex_ = -1;
    hasMouse_ = false;
    emit hoveredCandleChanged(nullptr);
    update();
  }

  void resizeEvent(QResizeEvent *event) override {
    QOpenGLWidget::resizeEvent(event);
    syncAnnotationStyleToolbar();
  }

  void mousePressEvent(QMouseEvent *event) override {
    setFocus(Qt::MouseFocusReason);
    if (annotationTool_ != AnnotationTool::None && event->button() == Qt::RightButton) {
      if (annotationTool_ == AnnotationTool::Polyline && drawingAnnotation_) finishPolyline();
      else if (!showPositionContextMenu(event->position(), event->globalPosition().toPoint())) showAnnotationContextMenu(event->position(), event->globalPosition().toPoint());
      return;
    }
    if (annotationTool_ == AnnotationTool::None && event->button() == Qt::RightButton) {
      if (!showPositionContextMenu(event->position(), event->globalPosition().toPoint())) showAnnotationContextMenu(event->position(), event->globalPosition().toPoint());
      return;
    }
    if (event->button() != Qt::LeftButton) return;
    if (toggleLayerAt(event->position())) {
      update();
      return;
    }
    if (annotationTool_ == AnnotationTool::None) {
      selectedAnnotation_ = annotationAt(event->position());
      if (selectedAnnotation_ >= 0) {
        startAnnotationDrag(event->position());
        syncAnnotationStyleToolbar();
        update();
        return;
      }
      syncAnnotationStyleToolbar();
    }
    if (annotationTool_ != AnnotationTool::None && plotRect().contains(event->position())) {
      handleAnnotationPress(event->position());
      return;
    }
    if (priceAxisRect().contains(event->position())) {
      yAxisScaling_ = true;
      dragStart_ = event->position().toPoint();
      axisPriceScale_ = manualPriceScale_;
      return;
    }
    if (timeAxisRect().contains(event->position())) {
      xAxisScaling_ = true;
      dragStart_ = event->position().toPoint();
      axisVisibleCount_ = visibleCount_;
      axisVisibleStart_ = visibleStart_;
      axisAnchorLocalX_ = std::clamp(event->position().x() - plotRect().left(), 0.0, plotRect().width());
      axisAnchorIndex_ = visibleStart_ + axisAnchorLocalX_ / std::max(0.05, barStep()) - 0.5;
      return;
    }
    dragging_ = true;
    dragStart_ = event->position().toPoint();
    dragVisibleStart_ = visibleStart_;
    double minPrice, maxPrice;
    visibleRange(minPrice, maxPrice);
    dragPriceRange_ = std::max(1e-9, maxPrice - minPrice);
    dragPriceOffset_ = manualPriceOffset_;
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      if (annotationDragMode_ != AnnotationDragMode::None) {
        finishAnnotationDrag();
        return;
      }
      if (drawingAnnotation_ && annotationTool_ != AnnotationTool::Polyline) {
        handleAnnotationRelease(event->position());
        return;
      }
      dragging_ = false;
      xAxisScaling_ = false;
      yAxisScaling_ = false;
    }
  }

  void wheelEvent(QWheelEvent *event) override {
    if (candles_.isEmpty()) return;
    const int before = visibleCount_;
    const QRectF plot = plotRect();
    const double oldStep = plot.width() / std::max(1, visibleCount_);
    const double anchorLocalX = std::clamp(event->position().x() - plot.left(), 0.0, plot.width());
    const bool anchorInPlot = plot.contains(event->position());
    const double anchorIndex = visibleStart_ + anchorLocalX / std::max(0.05, oldStep) - 0.5;
    const double factor = event->angleDelta().y() > 0 ? 0.94 : 1.065;
    visibleCount_ = std::clamp(static_cast<int>(std::round(visibleCount_ * factor)), 20, std::max(40, candleCount() + rightOffsetBars_));
    if (anchorInPlot) {
      const double newStep = plot.width() / std::max(1, visibleCount_);
      const double newLocalIndex = anchorLocalX / std::max(0.05, newStep) - 0.5;
      visibleStart_ = anchorIndex - newLocalIndex;
    } else if (before != visibleCount_) {
      visibleStart_ += (before - visibleCount_) / 2;
    }
    visibleStart_ = std::clamp(visibleStart_, 0.0, maxVisibleStart());
    requestMoreIfNeeded();
    emitOverlayRange();
    emitVisibleRange();
    scheduleRepaint();
  }

  void mouseDoubleClickEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton && annotationTool_ == AnnotationTool::Polyline) {
      finishPolyline();
      return;
    }
    if (event->button() == Qt::LeftButton && priceAxisRect().contains(event->position())) {
      manualPriceScale_ = 1.0;
      manualPriceOffset_ = 0.0;
      update();
    }
  }

private:
  QRectF plotRect() const {
    return rect().adjusted(16, 16, -74, -28);
  }

  QRectF priceAxisRect() const {
    const QRectF r = plotRect();
    return QRectF(r.right(), r.top(), width() - r.right(), r.height());
  }

  QRectF timeAxisRect() const {
    const QRectF r = plotRect();
    return QRectF(r.left(), r.bottom(), r.width(), height() - r.bottom());
  }

  double barStep() const {
    return plotRect().width() / std::max(1, visibleCount_);
  }

  int indexAt(double x) const {
    if (candles_.isEmpty() || !plotRect().contains(QPointF(x, plotRect().center().y()))) return -1;
    const int index = static_cast<int>(std::round(visibleStart_ + (x - plotRect().left()) / std::max(0.05, barStep()) - 0.5));
    return index >= 0 && index < candleCount() ? index : -1;
  }

  int candleCount() const {
    return static_cast<int>(candles_.size());
  }

  double maxVisibleStart() const {
    return std::max(0.0, double(candleCount() - visibleCount_ + rightOffsetBars_));
  }

  bool isAtRealtimeEdge() const {
    return maxVisibleStart() - visibleStart_ <= std::max(2.0, rightOffsetBars_ * 0.25);
  }

  QColor bg() const { return dark_ ? QColor("#0b100f") : QColor("#fffefa"); }
  QColor text() const { return dark_ ? QColor("#f4efe3") : QColor("#131916"); }
  QColor muted() const { return dark_ ? QColor("#a7b0a8") : QColor("#59635d"); }
  QColor grid() const { return dark_ ? QColor(230, 226, 211, 20) : QColor(23, 31, 27, 22); }
  QColor up() const { return QColor("#20c997"); }
  QColor down() const { return QColor("#ef5f78"); }

  QFont uiFont(int pixelSize, QFont::Weight weight = QFont::Normal) const {
    QFont f = font();
    f.setPixelSize(pixelSize);
    f.setWeight(weight);
    f.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::PreferQuality));
    return f;
  }

  QFont numberFont(int pixelSize, QFont::Weight weight = QFont::Medium) const {
    QFont f;
#ifdef Q_OS_WIN
    f.setFamilies({"Cascadia Mono", "Consolas", "Microsoft YaHei UI"});
#elif defined(Q_OS_MACOS)
    f.setFamilies({"SF Mono", "Menlo", "PingFang SC"});
#else
    f.setFamilies({"Noto Sans Mono CJK SC", "DejaVu Sans Mono", "Noto Sans CJK SC"});
#endif
    f.setPixelSize(pixelSize);
    f.setWeight(weight);
    f.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::PreferQuality));
    return f;
  }

  void visibleRange(double &minPrice, double &maxPrice) const {
    minPrice = std::numeric_limits<double>::max();
    maxPrice = std::numeric_limits<double>::lowest();
    const int start = std::max(0, static_cast<int>(std::floor(visibleStart_)));
    const int end = std::min(candleCount(), static_cast<int>(std::ceil(visibleStart_ + visibleCount_)));
    for (int i = start; i < end; ++i) {
      minPrice = std::min(minPrice, candles_[i].low);
      maxPrice = std::max(maxPrice, candles_[i].high);
    }
    if (!std::isfinite(minPrice) || !std::isfinite(maxPrice) || minPrice == maxPrice) {
      minPrice = 0;
      maxPrice = 1;
    }
    const double pad = (maxPrice - minPrice) * 0.08;
    minPrice -= pad;
    maxPrice += pad;
    if (manualPriceScale_ != 1.0) {
      const double mid = (minPrice + maxPrice) / 2.0;
      const double half = (maxPrice - minPrice) * manualPriceScale_ / 2.0;
      minPrice = mid - half;
      maxPrice = mid + half;
    }
    minPrice += manualPriceOffset_;
    maxPrice += manualPriceOffset_;
  }

  double yFor(double price, double minPrice, double maxPrice) const {
    const QRectF r = plotRect();
    return r.bottom() - ((price - minPrice) / (maxPrice - minPrice)) * r.height();
  }

  double priceForY(double y, double minPrice, double maxPrice) const {
    const QRectF r = plotRect();
    return maxPrice - ((y - r.top()) / r.height()) * (maxPrice - minPrice);
  }

  double indexForX(double x) const {
    return visibleStart_ + (x - plotRect().left()) / std::max(0.05, barStep()) - 0.5;
  }

  void paintBackground(QPainter &p) {
    p.fillRect(rect(), bg());
  }

  void paintChartMessage(QPainter &p) {
    const QRectF r = plotRect();
    p.setPen(QPen(grid(), 1));
    for (int i = 0; i <= 6; ++i) {
      const double y = r.top() + r.height() * i / 6.0;
      p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }
    for (int i = 0; i <= 8; ++i) {
      const double x = r.left() + r.width() * i / 8.0;
      p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }

    QRectF box(r.center().x() - 300, r.center().y() - 74, 600, 148);
    p.setPen(QPen(dark_ ? QColor(230, 226, 211, 46) : QColor(23, 31, 27, 48), 1));
    p.setBrush(dark_ ? QColor(14, 19, 17, 226) : QColor(255, 253, 247, 238));
    p.drawRect(box);

    p.setFont(uiFont(14, QFont::DemiBold));
    p.setPen(QColor("#ef5f78"));
    p.drawText(box.adjusted(18, 16, -18, 0), Qt::AlignLeft | Qt::AlignTop, "数据加载失败");

    p.setFont(uiFont(12, QFont::Medium));
    p.setPen(text());
    p.drawText(box.adjusted(18, 46, -18, -16), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, messageText_);
  }

  void paintGrid(QPainter &p, double minPrice, double maxPrice) {
    const QRectF r = plotRect();
    p.setPen(QPen(grid(), 1));
    p.setFont(numberFont(12, QFont::Medium));
    const int yTicks = 10;
    for (int i = 0; i <= yTicks; ++i) {
      const double y = r.top() + r.height() * i / yTicks;
      p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
      const double price = maxPrice - (maxPrice - minPrice) * i / yTicks;
      p.setPen(muted());
      p.drawText(QRectF(r.right() + 8, y - 9, 66, 18), Qt::AlignVCenter | Qt::AlignLeft, QString::number(price, 'f', price > 100 ? 0 : 2));
      p.setPen(QPen(grid(), 1));
    }
    for (int i = 0; i <= 8; ++i) {
      const double x = r.left() + r.width() * i / 8.0;
      p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
      const int candleIndex = static_cast<int>(std::round(visibleStart_ + (visibleCount_ - 1) * i / 8.0));
      if (candleIndex >= 0 && candleIndex < candleCount()) {
        const QString label = formatChartTime(candles_[candleIndex].ms, "MM-dd HH:mm");
        p.setPen(muted());
        p.drawText(QRectF(x - 48, r.bottom() + 8, 96, 18), Qt::AlignCenter, label);
        p.setPen(QPen(grid(), 1));
      }
    }
    p.setPen(QPen(dark_ ? QColor(230, 226, 211, 46) : QColor(23, 31, 27, 48), 1));
    p.drawLine(r.topRight(), r.bottomRight());
    p.drawLine(r.bottomLeft(), r.bottomRight());
  }

  void paintCandles(QPainter &p) {
    if (candles_.isEmpty()) return;
    p.save();
    p.setClipRect(plotRect());
    double minPrice, maxPrice;
    visibleRange(minPrice, maxPrice);
    const QRectF r = plotRect();
    const double step = barStep();
    const double bodyWidth = std::min(std::max(1.0, step * 0.72), std::max(1.0, step - 2.0));
    const int start = std::max(0, static_cast<int>(std::floor(visibleStart_)) - 1);
    const int end = std::min(candleCount(), static_cast<int>(std::ceil(visibleStart_ + visibleCount_)) + 1);
    for (int i = start; i < end; ++i) {
      const Candle &c = candles_[i];
      const double x = r.left() + (i - visibleStart_ + 0.5) * step;
      const QColor color = c.close >= c.open ? up() : down();
      const double yHigh = yFor(c.high, minPrice, maxPrice);
      const double yLow = yFor(c.low, minPrice, maxPrice);
      const double yOpen = yFor(c.open, minPrice, maxPrice);
      const double yClose = yFor(c.close, minPrice, maxPrice);
      p.setPen(QPen(color, 1));
      p.drawLine(QPointF(x, yHigh), QPointF(x, yLow));
      QRectF body(x - bodyWidth / 2.0, std::min(yOpen, yClose), bodyWidth, std::max(1.0, std::abs(yClose - yOpen)));
      p.fillRect(body, color);
      p.drawRect(body);
    }
    p.restore();
  }

  struct OverlayPoint {
    qint64 time = 0;
    double price = std::numeric_limits<double>::quiet_NaN();
  };

  struct OverlayStyle {
    QColor stroke;
    QColor fill;
    QColor textColor;
    QColor profitFill;
    QColor lossFill;
    QColor entryLine;
    QColor slLine;
    QColor tpLine;
    int strokeWidth = 1;
    double strokeOpacity = 1.0;
    double fillOpacity = 0.35;
    QVector<qreal> dash;
    int fontSize = 10;
  };

  struct OverlayLayer {
    QString id;
    QString type;
    QString group;
    bool visible = true;
    int zIndex = 0;
    QVector<OverlayPoint> points;
    qint64 from = 0;
    qint64 to = 0;
    qint64 entryTime = 0;
    qint64 exitTime = 0;
    double price = std::numeric_limits<double>::quiet_NaN();
    double top = std::numeric_limits<double>::quiet_NaN();
    double bottom = std::numeric_limits<double>::quiet_NaN();
    double entry = std::numeric_limits<double>::quiet_NaN();
    double sl = std::numeric_limits<double>::quiet_NaN();
    double tp1 = std::numeric_limits<double>::quiet_NaN();
    double tp2 = std::numeric_limits<double>::quiet_NaN();
    double quantity = std::numeric_limits<double>::quiet_NaN();
    double amount = std::numeric_limits<double>::quiet_NaN();
    QString side;
    QString shape;
    QString text;
    QString anchor;
    QJsonObject data;
    OverlayStyle style;
  };

  bool layerGroupEnabled(const OverlayLayer &layer) const {
    return layer.visible && layerGroupVisible_.value(layer.group, true);
  }

  bool layerVisibleInTime(const OverlayLayer &layer) const {
    qint64 start = 0;
    qint64 end = 0;
    if (!layer.points.isEmpty()) {
      start = layer.points.first().time;
      end = layer.points.first().time;
      for (const OverlayPoint &point : layer.points) {
        start = std::min(start, point.time);
        end = std::max(end, point.time);
      }
    } else {
      start = layer.from > 0 ? layer.from : layer.entryTime;
      end = layer.to > 0 ? layer.to : layer.exitTime;
      if (start <= 0 && layer.price > 0) start = candles_.first().ms;
      if (end <= 0 && start > 0) end = candles_.last().ms;
    }
    if (start <= 0 || end <= 0) return true;
    return timeWindowVisible(start, end);
  }

  void paintGenericLayers(QPainter &p, double minPrice, double maxPrice) {
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const OverlayLayer &layer : parsedLayers_) {
      if (!layerGroupEnabled(layer) || !layerVisibleInTime(layer)) continue;
      if (layer.type == "line" || layer.type == "ray" || layer.type == "polyline") drawGenericLineLayer(p, layer, minPrice, maxPrice);
      else if (layer.type == "box" || layer.type == "range") drawGenericBoxLayer(p, layer, minPrice, maxPrice);
      else if (layer.type == "position") drawGenericPositionLayer(p, layer, minPrice, maxPrice);
      else if (layer.type == "label") drawGenericLabelLayer(p, layer, minPrice, maxPrice);
      else if (layer.type == "marker") drawGenericMarkerLayer(p, layer, minPrice, maxPrice);
      else if (layer.type == "priceline") drawGenericPriceLineLayer(p, layer, minPrice, maxPrice);
    }
  }

  void drawGenericLineLayer(QPainter &p, const OverlayLayer &layer, double minPrice, double maxPrice) {
    if (layer.points.size() < 2) return;
    p.setPen(layerPen(layer.style));
    QPolygonF points;
    for (const OverlayPoint &point : layer.points) points << pointAtTime(point.time, point.price, minPrice, maxPrice);
    p.drawPolyline(points);
    if (!layer.text.isEmpty()) {
      p.setFont(uiFont(layer.style.fontSize, QFont::DemiBold));
      p.setPen(withOpacity(layer.style.textColor, layer.style.strokeOpacity));
      p.drawText(points.last() + QPointF(6, -6), layer.text);
    }
  }

  void drawGenericBoxLayer(QPainter &p, const OverlayLayer &layer, double minPrice, double maxPrice) {
    const qint64 from = layer.from > 0 ? layer.from : (layer.points.isEmpty() ? 0 : layer.points.first().time);
    const qint64 to = layer.to > 0 ? layer.to : (layer.points.size() > 1 ? layer.points.last().time : candles_.last().ms);
    double top = layer.top;
    double bottom = layer.bottom;
    if ((!std::isfinite(top) || !std::isfinite(bottom)) && layer.type == "range" && std::isfinite(layer.price)) {
      top = layer.price;
      bottom = layer.price;
    }
    if (from <= 0 || to <= 0 || !std::isfinite(top) || !std::isfinite(bottom)) return;
    if (std::abs(top - bottom) < 1e-9) {
      drawLineAt(p, from, to, top, withOpacity(layer.style.stroke, layer.style.strokeOpacity), layer.style.dash.isEmpty() ? Qt::SolidLine : Qt::DashLine, minPrice, maxPrice);
    } else {
      QRectF rect(pointAtTime(from, top, minPrice, maxPrice), pointAtTime(to, bottom, minPrice, maxPrice));
      rect = rect.normalized();
      p.fillRect(rect, withOpacity(layer.style.fill, layer.style.fillOpacity));
      p.setBrush(Qt::NoBrush);
      p.setPen(layerPen(layer.style));
      p.drawRect(rect);
      const QString text = layer.text.isEmpty() ? layer.group : layer.text;
      if (!text.isEmpty()) {
        p.setFont(uiFont(layer.style.fontSize, QFont::DemiBold));
        p.setPen(withOpacity(layer.style.textColor, 1.0));
        p.drawText(rect.topLeft() + QPointF(4, -4), text);
      }
    }
  }

  void drawGenericPriceLineLayer(QPainter &p, const OverlayLayer &layer, double minPrice, double maxPrice) {
    if (!std::isfinite(layer.price)) return;
    const qint64 from = layer.from > 0 ? layer.from : candles_.first().ms;
    const qint64 to = layer.to > 0 ? layer.to : candles_.last().ms + 10 * barIntervalMs();
    drawLineAt(p, from, to, layer.price, withOpacity(layer.style.stroke, layer.style.strokeOpacity), layer.style.dash.isEmpty() ? Qt::SolidLine : Qt::DashLine, minPrice, maxPrice);
    if (!layer.text.isEmpty()) {
      p.setFont(uiFont(layer.style.fontSize, QFont::DemiBold));
      p.setPen(withOpacity(layer.style.textColor, 1.0));
      p.drawText(pointAtTime(to, layer.price, minPrice, maxPrice) + QPointF(6, 4), layer.text);
    }
  }

  void drawGenericLabelLayer(QPainter &p, const OverlayLayer &layer, double minPrice, double maxPrice) {
    const qint64 time = layer.from > 0 ? layer.from : (layer.points.isEmpty() ? 0 : layer.points.first().time);
    const double price = std::isfinite(layer.price) ? layer.price : (layer.points.isEmpty() ? std::numeric_limits<double>::quiet_NaN() : layer.points.first().price);
    if (time <= 0 || !std::isfinite(price) || layer.text.isEmpty()) return;
    const QPointF anchor = pointAtTime(time, price, minPrice, maxPrice);
    p.setFont(uiFont(layer.style.fontSize, QFont::DemiBold));
    const QFontMetrics fm(p.font());
    QRectF rect(anchor.x() + 6, anchor.y() - 20, fm.horizontalAdvance(layer.text) + 12, 20);
    if (layer.anchor.contains("center")) rect.moveCenter(anchor);
    p.setPen(QPen(withOpacity(layer.style.stroke, layer.style.strokeOpacity), 1));
    p.setBrush(withOpacity(layer.style.fill, std::max(0.78, layer.style.fillOpacity)));
    p.drawRoundedRect(rect, 3, 3);
    p.setPen(layer.style.textColor);
    p.drawText(rect, Qt::AlignCenter, layer.text);
  }

  void drawGenericMarkerLayer(QPainter &p, const OverlayLayer &layer, double minPrice, double maxPrice) {
    const qint64 time = layer.from > 0 ? layer.from : (layer.points.isEmpty() ? 0 : layer.points.first().time);
    const double price = std::isfinite(layer.price) ? layer.price : (layer.points.isEmpty() ? std::numeric_limits<double>::quiet_NaN() : layer.points.first().price);
    if (time <= 0 || !std::isfinite(price)) return;
    const QPointF pos = pointAtTime(time, price, minPrice, maxPrice);
    const QColor color = withOpacity(layer.style.stroke, layer.style.strokeOpacity);
    p.setPen(QPen(color, 1.25));
    p.setBrush(color);
    const double size = std::max(5.0, static_cast<double>(layer.style.fontSize));
    if (layer.shape == "triangle-up" || layer.shape == "triangle_up") {
      QPolygonF triangle;
      triangle << QPointF(pos.x(), pos.y() - size) << QPointF(pos.x() - size * 0.72, pos.y() + size * 0.62) << QPointF(pos.x() + size * 0.72, pos.y() + size * 0.62);
      p.drawPolygon(triangle);
    } else if (layer.shape == "triangle-down" || layer.shape == "triangle_down") {
      QPolygonF triangle;
      triangle << QPointF(pos.x(), pos.y() + size) << QPointF(pos.x() - size * 0.72, pos.y() - size * 0.62) << QPointF(pos.x() + size * 0.72, pos.y() - size * 0.62);
      p.drawPolygon(triangle);
    } else if (layer.shape == "square") {
      p.drawRect(QRectF(pos.x() - size * 0.55, pos.y() - size * 0.55, size * 1.1, size * 1.1));
    } else {
      p.drawEllipse(pos, size * 0.48, size * 0.48);
    }
    if (!layer.text.isEmpty()) {
      p.setFont(uiFont(layer.style.fontSize, QFont::DemiBold));
      p.setPen(color);
      p.drawText(pos + QPointF(size * 0.75, 4), layer.text);
    }
  }

  void drawGenericPositionLayer(QPainter &p, const OverlayLayer &layer, double minPrice, double maxPrice) {
    const qint64 start = layer.entryTime > 0 ? layer.entryTime : layer.from;
    const qint64 end = layer.exitTime > 0 ? layer.exitTime : (layer.to > 0 ? layer.to : candles_.last().ms + 5 * barIntervalMs());
    if (start <= 0 || end <= 0 || !std::isfinite(layer.entry)) return;
    const bool isLong = layer.side != "short";
    const double entry = layer.entry;
    const double sl = layer.sl;
    const double tp1 = layer.tp1;
    const double tp2 = layer.tp2;
    if (std::isfinite(sl) && (isLong ? sl < entry : sl > entry)) {
      drawRangeArea(p, start, end, entry, sl, withOpacity(layer.style.lossFill, layer.style.fillOpacity), minPrice, maxPrice);
      drawLineAt(p, start, end, sl, withOpacity(layer.style.slLine, 0.95), Qt::SolidLine, minPrice, maxPrice);
    }
    const double rewardBoundary = std::isfinite(tp2) ? tp2 : tp1;
    if (std::isfinite(rewardBoundary) && (isLong ? rewardBoundary > entry : rewardBoundary < entry)) {
      drawRangeArea(p, start, end, entry, rewardBoundary, withOpacity(layer.style.profitFill, layer.style.fillOpacity), minPrice, maxPrice);
    }
    if (std::isfinite(tp1)) drawLineAt(p, start, end, tp1, withOpacity(layer.style.tpLine, 0.82), Qt::DashLine, minPrice, maxPrice);
    if (std::isfinite(tp2) && (!std::isfinite(tp1) || std::abs(tp2 - tp1) > 1e-9)) drawLineAt(p, start, end, tp2, withOpacity(layer.style.tpLine, 0.95), Qt::SolidLine, minPrice, maxPrice);
    drawLineAt(p, start, end, entry, withOpacity(layer.style.entryLine, 0.96), Qt::SolidLine, minPrice, maxPrice);
    if (markerVisible_) {
      const QString label = layer.text.isEmpty() ? (isLong ? "L" : "S") : layer.text;
      drawBadgeMarker(p, start, entry, isLong, isLong ? up() : down(), label, minPrice, maxPrice);
    }
    registerGenericPositionHitbox(layer, start, end, minPrice, maxPrice);
  }

  struct AnnotationPoint {
    double index = 0.0;
    double price = 0.0;
  };

  struct AnnotationStyle {
    QColor line = QColor("#f0b64f");
    QColor fill = QColor("#6ed7f6");
    QColor profit = QColor("#20c997");
    QColor loss = QColor("#ef5f78");
    int lineWidth = 1;
    int opacity = 58;
  };

  enum class AnnotationDragMode { None, Move, ResizePoint };

  struct ManualAnnotation {
    AnnotationTool tool = AnnotationTool::None;
    QVector<AnnotationPoint> points;
    AnnotationStyle style;
  };

  QPointF cursorPosition(const QPointF &pos) const {
    if (!magnetEnabled_ || !plotRect().contains(pos) || candles_.isEmpty()) return pos;
    double minPrice, maxPrice;
    visibleRange(minPrice, maxPrice);
    const AnnotationPoint point = chartPointFromPosition(pos);
    return annotationPoint(point, minPrice, maxPrice);
  }

  AnnotationPoint chartPointFromPosition(const QPointF &pos) const {
    double minPrice, maxPrice;
    visibleRange(minPrice, maxPrice);
    AnnotationPoint point{indexForX(pos.x()), priceForY(pos.y(), minPrice, maxPrice)};
    if (!magnetEnabled_ || candles_.isEmpty()) return point;
    const int center = std::clamp(static_cast<int>(std::round(point.index)), 0, candleCount() - 1);
    double bestPrice = point.price;
    int bestIndex = center;
    double bestDistance = std::numeric_limits<double>::max();
    const double maxXDistance = std::max(14.0, barStep() * 1.2);
    for (int i = std::max(0, center - 1); i <= std::min(candleCount() - 1, center + 1); ++i) {
      const Candle &c = candles_[i];
      const double candleX = pointAtIndex(i, c.close, minPrice, maxPrice).x();
      if (std::abs(candleX - pos.x()) > maxXDistance) continue;
      const QVector<double> prices{c.open, c.high, c.low, c.close};
      for (double price : prices) {
        const double y = yFor(price, minPrice, maxPrice);
        const double distance = std::hypot((candleX - pos.x()) * 0.65, y - pos.y());
        if (distance < bestDistance) {
          bestDistance = distance;
          bestPrice = price;
          bestIndex = i;
        }
      }
    }
    if (bestDistance <= 22.0) return AnnotationPoint{static_cast<double>(bestIndex), bestPrice};
    return point;
  }

  QPointF annotationPoint(const AnnotationPoint &point, double minPrice, double maxPrice) const {
    return pointAtIndex(point.index, point.price, minPrice, maxPrice);
  }

  QRectF annotationRect(const ManualAnnotation &annotation, double minPrice, double maxPrice) const {
    if (annotation.points.size() < 2) return {};
    return QRectF(annotationPoint(annotation.points[0], minPrice, maxPrice),
                  annotationPoint(annotation.points[1], minPrice, maxPrice)).normalized();
  }

  void handleAnnotationPress(const QPointF &pos) {
    const AnnotationPoint point = chartPointFromPosition(pos);
    if (annotationTool_ == AnnotationTool::LongBlock || annotationTool_ == AnnotationTool::ShortBlock) {
      addPositionAnnotation(point, annotationTool_);
      resetAnnotationTool();
      return;
    }
    if (annotationTool_ == AnnotationTool::HorizontalLine || annotationTool_ == AnnotationTool::VerticalLine) {
      manualAnnotations_.push_back({annotationTool_, {point}, defaultAnnotationStyle(annotationTool_)});
      selectedAnnotation_ = manualAnnotations_.size() - 1;
      rememberAnnotationStyle(manualAnnotations_.last());
      syncAnnotationStyleToolbar();
      update();
      resetAnnotationTool();
      return;
    }
    if (annotationTool_ == AnnotationTool::Polyline) {
      draftPolyline_.push_back(point);
      drawingAnnotation_ = true;
      draftPoint_ = point;
      update();
      return;
    }
    drawingAnnotation_ = true;
    draftStart_ = point;
    draftPoint_ = point;
  }

  void handleAnnotationRelease(const QPointF &pos) {
    const AnnotationPoint end = chartPointFromPosition(pos);
    drawingAnnotation_ = false;
    if (std::abs(end.index - draftStart_.index) < 0.05 && std::abs(end.price - draftStart_.price) < 1e-9) {
      update();
      return;
    }
    if (annotationTool_ == AnnotationTool::SegmentLine) {
      manualAnnotations_.push_back({annotationTool_, {draftStart_, {end.index, draftStart_.price}}, defaultAnnotationStyle(annotationTool_)});
    } else {
      manualAnnotations_.push_back({annotationTool_, {draftStart_, end}, defaultAnnotationStyle(annotationTool_)});
    }
    selectedAnnotation_ = manualAnnotations_.size() - 1;
    rememberAnnotationStyle(manualAnnotations_.last());
    syncAnnotationStyleToolbar();
    update();
    resetAnnotationTool();
  }

  void finishPolyline() {
    if (annotationTool_ == AnnotationTool::Polyline && draftPolyline_.size() >= 2) {
      manualAnnotations_.push_back({AnnotationTool::Polyline, draftPolyline_, defaultAnnotationStyle(AnnotationTool::Polyline)});
      selectedAnnotation_ = manualAnnotations_.size() - 1;
      rememberAnnotationStyle(manualAnnotations_.last());
      syncAnnotationStyleToolbar();
    }
    draftPolyline_.clear();
    drawingAnnotation_ = false;
    update();
  }

  AnnotationStyle defaultAnnotationStyle(AnnotationTool tool) const {
    const int key = static_cast<int>(tool);
    if (annotationDefaultStyles_.contains(key)) return annotationDefaultStyles_.value(key);
    AnnotationStyle style;
    style.line = dark_ ? QColor("#f0b64f") : QColor("#b27a17");
    style.fill = QColor("#6ed7f6");
    style.lineWidth = 1;
    style.opacity = 58;
    if (tool == AnnotationTool::LongBlock) {
      style.line = QColor("#20c997");
      style.fill = QColor("#20c997");
      style.profit = QColor("#20c997");
      style.loss = QColor("#ef5f78");
      style.opacity = dark_ ? 72 : 54;
    } else if (tool == AnnotationTool::ShortBlock) {
      style.line = QColor("#ef5f78");
      style.fill = QColor("#ef5f78");
      style.profit = QColor("#20c997");
      style.loss = QColor("#ef5f78");
      style.opacity = dark_ ? 72 : 54;
    }
    return style;
  }

  void rememberAnnotationStyle(const ManualAnnotation &annotation) {
    annotationDefaultStyles_.insert(static_cast<int>(annotation.tool), annotation.style);
  }

  bool isPositionAnnotation(const ManualAnnotation &annotation) const {
    return (annotation.tool == AnnotationTool::LongBlock || annotation.tool == AnnotationTool::ShortBlock) && annotation.points.size() >= 3;
  }

  QVector<AnnotationPoint> positionAnnotationHandlePoints(const ManualAnnotation &annotation) const {
    if (!isPositionAnnotation(annotation)) return annotation.points;
    const AnnotationPoint entry = annotation.points[0];
    const AnnotationPoint profit = annotation.points[1];
    const AnnotationPoint loss = annotation.points[2];
    const double endIndex = std::max(profit.index, loss.index);
    const double top = std::max({entry.price, profit.price, loss.price});
    const double bottom = std::min({entry.price, profit.price, loss.price});
    return {
      {entry.index, top},
      {entry.index, bottom},
      {endIndex, entry.price}
    };
  }

  void addPositionAnnotation(const AnnotationPoint &entry, AnnotationTool tool) {
    double minPrice, maxPrice;
    visibleRange(minPrice, maxPrice);
    const double half = std::max((maxPrice - minPrice) * 0.075, std::abs(entry.price) * 0.0015);
    const double endIndex = entry.index + std::max(24.0, visibleCount_ * 0.22);
    AnnotationPoint profit{endIndex, tool == AnnotationTool::LongBlock ? entry.price + half : entry.price - half};
    AnnotationPoint loss{endIndex, tool == AnnotationTool::LongBlock ? entry.price - half : entry.price + half};
    manualAnnotations_.push_back({tool, {entry, profit, loss}, defaultAnnotationStyle(tool)});
    selectedAnnotation_ = manualAnnotations_.size() - 1;
    rememberAnnotationStyle(manualAnnotations_.last());
    syncAnnotationStyleToolbar();
    update();
  }

  void undoAnnotation() {
    if (drawingAnnotation_) {
      drawingAnnotation_ = false;
      draftPolyline_.clear();
      update();
      return;
    }
    if (manualAnnotations_.isEmpty()) return;
    manualAnnotations_.removeLast();
    selectedAnnotation_ = manualAnnotations_.isEmpty() ? -1 : std::min(selectedAnnotation_, static_cast<int>(manualAnnotations_.size()) - 1);
    update();
  }

  void deleteSelectedAnnotation() {
    if (selectedAnnotation_ < 0 || selectedAnnotation_ >= manualAnnotations_.size()) return;
    manualAnnotations_.removeAt(selectedAnnotation_);
    selectedAnnotation_ = -1;
    syncAnnotationStyleToolbar();
    update();
  }

  QColor annotationColor(const ManualAnnotation &annotation) const {
    if (annotation.style.line.isValid()) {
      QColor color = annotation.style.line;
      if (annotation.tool == AnnotationTool::LongBlock || annotation.tool == AnnotationTool::ShortBlock) color.setAlpha(annotation.style.opacity);
      return color;
    }
    if (annotation.tool == AnnotationTool::LongBlock) return QColor(32, 201, 151, dark_ ? 78 : 58);
    if (annotation.tool == AnnotationTool::ShortBlock) return QColor(239, 95, 120, dark_ ? 78 : 58);
    return dark_ ? QColor(240, 182, 79, 220) : QColor(178, 122, 23, 230);
  }

  void drawManualAnnotation(QPainter &p, const ManualAnnotation &annotation, double minPrice, double maxPrice, bool draft = false, bool selected = false) {
    if (annotation.points.isEmpty()) return;
    const QRectF r = plotRect();
    QColor color = annotationColor(annotation);
    if (draft) color.setAlpha(std::min(230, color.alpha() + 45));
    QColor lineColor = annotation.style.line.isValid() ? annotation.style.line : color;
    lineColor.setAlpha(draft ? 245 : 220);
    const int lineWidth = std::max(1, annotation.style.lineWidth);
    p.save();
    p.setClipRect(r);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (annotation.tool == AnnotationTool::HorizontalLine) {
      const double y = yFor(annotation.points[0].price, minPrice, maxPrice);
      p.setPen(QPen(lineColor, lineWidth));
      p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    } else if (annotation.tool == AnnotationTool::VerticalLine) {
      const double x = pointAtIndex(annotation.points[0].index, annotation.points[0].price, minPrice, maxPrice).x();
      p.setPen(QPen(lineColor, lineWidth));
      p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    } else if (annotation.tool == AnnotationTool::SegmentLine && annotation.points.size() >= 2) {
      p.setPen(QPen(lineColor, lineWidth));
      p.drawLine(annotationPoint(annotation.points[0], minPrice, maxPrice), annotationPoint(annotation.points[1], minPrice, maxPrice));
    } else if (annotation.tool == AnnotationTool::Polyline && annotation.points.size() >= 2) {
      QPolygonF polyline;
      for (const AnnotationPoint &point : annotation.points) polyline << annotationPoint(point, minPrice, maxPrice);
      p.setPen(QPen(lineColor, lineWidth));
      p.drawPolyline(polyline);
    } else if ((annotation.tool == AnnotationTool::Rectangle || annotation.tool == AnnotationTool::LongBlock || annotation.tool == AnnotationTool::ShortBlock) && annotation.points.size() >= 2) {
      if ((annotation.tool == AnnotationTool::LongBlock || annotation.tool == AnnotationTool::ShortBlock) && annotation.points.size() >= 3) {
        drawPositionAnnotation(p, annotation, minPrice, maxPrice, lineColor, draft, selected);
        p.restore();
        return;
      }
      QRectF box = annotationRect(annotation, minPrice, maxPrice);
      const bool block = annotation.tool == AnnotationTool::LongBlock || annotation.tool == AnnotationTool::ShortBlock;
      QColor fill = annotation.style.fill.isValid() ? annotation.style.fill : color;
      fill.setAlpha(annotation.style.opacity);
      p.setPen(QPen(lineColor, lineWidth));
      p.setBrush(block ? fill : Qt::NoBrush);
      p.drawRect(box);
      if (block) {
        p.setPen(lineColor);
        p.setFont(uiFont(11, QFont::DemiBold));
        p.drawText(box.adjusted(6, 4, -6, -4), Qt::AlignTop | Qt::AlignLeft, annotation.tool == AnnotationTool::LongBlock ? "L" : "S");
      }
    }
    if (selected) drawAnnotationSelection(p, annotation, minPrice, maxPrice);
    p.restore();
  }

  void paintManualAnnotations(QPainter &p, double minPrice, double maxPrice) {
    for (int i = 0; i < manualAnnotations_.size(); ++i) drawManualAnnotation(p, manualAnnotations_[i], minPrice, maxPrice, false, i == selectedAnnotation_);
    if (drawingAnnotation_) {
      if (annotationTool_ == AnnotationTool::Polyline) {
        QVector<AnnotationPoint> points = draftPolyline_;
        if (!points.isEmpty()) points.push_back(draftPoint_);
        drawManualAnnotation(p, {AnnotationTool::Polyline, points, defaultAnnotationStyle(AnnotationTool::Polyline)}, minPrice, maxPrice, true);
      } else {
        const AnnotationPoint end = annotationTool_ == AnnotationTool::SegmentLine ? AnnotationPoint{draftPoint_.index, draftStart_.price} : draftPoint_;
        drawManualAnnotation(p, {annotationTool_, {draftStart_, end}, defaultAnnotationStyle(annotationTool_)}, minPrice, maxPrice, true);
      }
    }
  }

  void drawPositionAnnotation(QPainter &p, const ManualAnnotation &annotation, double minPrice, double maxPrice, const QColor &lineColor, bool draft, bool selected) {
    const AnnotationPoint entry = annotation.points[0];
    const AnnotationPoint profit = annotation.points[1];
    const AnnotationPoint loss = annotation.points[2];
    const bool isLong = annotation.tool == AnnotationTool::LongBlock;
    const double endIndex = std::max({entry.index, profit.index, loss.index});
    const double top = std::max({entry.price, profit.price, loss.price});
    const double bottom = std::min({entry.price, profit.price, loss.price});
    const qint64 startMs = timeForIndex(entry.index);
    const qint64 endMs = timeForIndex(endIndex);
    QColor profitColor = annotation.style.profit;
    QColor lossColor = annotation.style.loss;
    profitColor.setAlpha(annotation.style.opacity);
    lossColor.setAlpha(annotation.style.opacity);
    if (draft) {
      profitColor.setAlpha(std::min(160, profitColor.alpha() + 35));
      lossColor.setAlpha(std::min(160, lossColor.alpha() + 35));
    }
    const double profitBoundary = isLong ? top : bottom;
    const double lossBoundary = isLong ? bottom : top;
    drawRangeArea(p, startMs, endMs, entry.price, profitBoundary, profitColor, minPrice, maxPrice);
    drawRangeArea(p, startMs, endMs, entry.price, lossBoundary, lossColor, minPrice, maxPrice);
    p.setPen(QPen(lineColor, std::max(1, annotation.style.lineWidth), Qt::SolidLine));
    p.drawLine(pointAtTime(startMs, entry.price, minPrice, maxPrice), pointAtTime(endMs, entry.price, minPrice, maxPrice));
    QColor profitLine = annotation.style.profit;
    QColor lossLine = annotation.style.loss;
    profitLine.setAlpha(225);
    lossLine.setAlpha(225);
    p.setPen(QPen(profitLine, std::max(1, annotation.style.lineWidth), Qt::DashLine));
    p.drawLine(pointAtTime(startMs, profitBoundary, minPrice, maxPrice), pointAtTime(endMs, profitBoundary, minPrice, maxPrice));
    p.setPen(QPen(lossLine, std::max(1, annotation.style.lineWidth), Qt::DashLine));
    p.drawLine(pointAtTime(startMs, lossBoundary, minPrice, maxPrice), pointAtTime(endMs, lossBoundary, minPrice, maxPrice));
    drawPositionAnnotationLabels(p, annotation, startMs, endMs, entry.price, profitBoundary, lossBoundary, minPrice, maxPrice);
    if (selected) drawAnnotationSelection(p, annotation, minPrice, maxPrice);
  }

  void drawPositionAnnotationLabels(QPainter &p, const ManualAnnotation &annotation, qint64 startMs, qint64 endMs, double entry, double profitBoundary, double lossBoundary, double minPrice, double maxPrice) {
    const double risk = std::abs(entry - lossBoundary);
    const double reward = std::abs(profitBoundary - entry);
    const double ratio = risk > 1e-12 ? reward / risk : std::numeric_limits<double>::quiet_NaN();
    const bool isLong = annotation.tool == AnnotationTool::LongBlock;
    const double top = std::max(profitBoundary, lossBoundary);
    const double bottom = std::min(profitBoundary, lossBoundary);
    const qint64 midMs = startMs + (endMs - startMs) / 2;
    auto badge = [&](const QPointF &anchor, const QString &text, const QColor &color, Qt::Alignment align) {
      if (text.isEmpty()) return;
      QFont font = uiFont(10, QFont::DemiBold);
      p.setFont(font);
      const QFontMetrics fm(font);
      const QSize size(fm.horizontalAdvance(text) + 12, 20);
      QPointF topLeft = anchor;
      if (align & Qt::AlignHCenter) topLeft.rx() -= size.width() / 2.0;
      else if (align & Qt::AlignRight) topLeft.rx() -= size.width();
      if (align & Qt::AlignVCenter) topLeft.ry() -= size.height() / 2.0;
      else if (align & Qt::AlignBottom) topLeft.ry() -= size.height();
      QRectF rect(topLeft, size);
      rect = rect.intersected(plotRect().adjusted(2, 2, -2, -2));
      QColor fill = color;
      fill.setAlpha(235);
      p.setPen(Qt::NoPen);
      p.setBrush(fill);
      p.drawRoundedRect(rect, 3, 3);
      p.setPen(Qt::white);
      p.drawText(rect, Qt::AlignCenter, text);
    };
    const QColor profitColor = annotation.style.profit.isValid() ? annotation.style.profit : QColor("#20c997");
    const QColor lossColor = annotation.style.loss.isValid() ? annotation.style.loss : QColor("#ef5f78");
    const QString ratioText = std::isfinite(ratio) ? QString("Risk/reward ratio: %1").arg(ratio, 0, 'f', 2) : "Risk/reward ratio: --";
    badge(pointAtTime(midMs, entry, minPrice, maxPrice) + QPointF(0, -10), ratioText, isLong ? profitColor : lossColor, Qt::AlignHCenter | Qt::AlignBottom);
    badge(pointAtTime(startMs, profitBoundary, minPrice, maxPrice) + QPointF(8, 0), QString("Reward: %1").arg(reward, 0, 'f', 2), profitColor, Qt::AlignLeft | (profitBoundary == top ? Qt::AlignBottom : Qt::AlignTop));
    badge(pointAtTime(startMs, lossBoundary, minPrice, maxPrice) + QPointF(8, 0), QString("Risk: %1").arg(risk, 0, 'f', 2), lossColor, Qt::AlignLeft | (lossBoundary == bottom ? Qt::AlignTop : Qt::AlignBottom));
  }

  void drawAnnotationSelection(QPainter &p, const ManualAnnotation &annotation, double minPrice, double maxPrice) {
    p.save();
    p.setPen(QPen(QColor("#f0b64f"), 1.4));
    p.setBrush(QColor("#f0b64f"));
    if (isPositionAnnotation(annotation)) {
      for (const AnnotationPoint &point : positionAnnotationHandlePoints(annotation)) {
        const QPointF pos = annotationPoint(point, minPrice, maxPrice);
        QRectF handle(pos.x() - 4, pos.y() - 4, 8, 8);
        p.setBrush(QColor("#fffdf7"));
        p.drawRect(handle);
      }
      p.restore();
      return;
    }
    for (const AnnotationPoint &point : annotation.points) {
      const QPointF pos = annotationPoint(point, minPrice, maxPrice);
      QRectF handle(pos.x() - 3, pos.y() - 3, 6, 6);
      p.drawRect(handle);
    }
    p.restore();
  }

  QRectF annotationBounds(const ManualAnnotation &annotation, double minPrice, double maxPrice) const {
    QRectF bounds;
    const QVector<AnnotationPoint> points = isPositionAnnotation(annotation) ? positionAnnotationHandlePoints(annotation) : annotation.points;
    for (const AnnotationPoint &point : points) {
      const QPointF pos = annotationPoint(point, minPrice, maxPrice);
      const QRectF handle(pos.x() - 6, pos.y() - 6, 12, 12);
      bounds = bounds.isNull() ? handle : bounds.united(handle);
    }
    if (isPositionAnnotation(annotation) && annotation.points.size() >= 3) {
      const AnnotationPoint entry = annotation.points[0];
      const double endIndex = std::max(annotation.points[1].index, annotation.points[2].index);
      const double top = std::max({entry.price, annotation.points[1].price, annotation.points[2].price});
      const double bottom = std::min({entry.price, annotation.points[1].price, annotation.points[2].price});
      bounds = bounds.united(QRectF(annotationPoint({entry.index, top}, minPrice, maxPrice), annotationPoint({endIndex, bottom}, minPrice, maxPrice)).normalized());
    }
    return bounds.adjusted(-8, -8, 8, 8);
  }

  int annotationHandleAt(const ManualAnnotation &annotation, const QPointF &pos, double minPrice, double maxPrice) const {
    if (isPositionAnnotation(annotation)) {
      const QVector<AnnotationPoint> handles = positionAnnotationHandlePoints(annotation);
      for (int i = 0; i < handles.size(); ++i) {
        const QPointF handle = annotationPoint(handles[i], minPrice, maxPrice);
        if (QRectF(handle.x() - 8, handle.y() - 8, 16, 16).contains(pos)) return positionAnnotationHandleBase_ + i;
      }
      return -1;
    }
    for (int i = 0; i < annotation.points.size(); ++i) {
      const QPointF handle = annotationPoint(annotation.points[i], minPrice, maxPrice);
      if (QRectF(handle.x() - 7, handle.y() - 7, 14, 14).contains(pos)) return i;
    }
    return -1;
  }

  int annotationAt(const QPointF &pos) const {
    double minPrice, maxPrice;
    visibleRange(minPrice, maxPrice);
    for (int i = manualAnnotations_.size() - 1; i >= 0; --i) {
      const ManualAnnotation &annotation = manualAnnotations_[i];
      if (annotationBounds(annotation, minPrice, maxPrice).contains(pos)) return i;
    }
    return -1;
  }

  void startAnnotationDrag(const QPointF &pos) {
    if (selectedAnnotation_ < 0 || selectedAnnotation_ >= manualAnnotations_.size()) return;
    double minPrice, maxPrice;
    visibleRange(minPrice, maxPrice);
    annotationDragOriginal_ = manualAnnotations_[selectedAnnotation_].points;
    annotationDragStart_ = chartPointFromPosition(pos);
    const int handle = annotationHandleAt(manualAnnotations_[selectedAnnotation_], pos, minPrice, maxPrice);
    if (handle >= 0) {
      annotationDragMode_ = AnnotationDragMode::ResizePoint;
      annotationDragPoint_ = handle;
    } else {
      annotationDragMode_ = AnnotationDragMode::Move;
      annotationDragPoint_ = -1;
    }
  }

  void updateAnnotationDrag(const QPointF &pos) {
    if (selectedAnnotation_ < 0 || selectedAnnotation_ >= manualAnnotations_.size()) return;
    ManualAnnotation &annotation = manualAnnotations_[selectedAnnotation_];
    const AnnotationPoint current = chartPointFromPosition(pos);
    if (annotationDragMode_ == AnnotationDragMode::ResizePoint && isPositionAnnotation(annotation) && annotationDragPoint_ >= positionAnnotationHandleBase_) {
      annotation.points = annotationDragOriginal_;
      const int handle = annotationDragPoint_ - positionAnnotationHandleBase_;
      const double top = std::max({annotation.points[0].price, annotation.points[1].price, annotation.points[2].price});
      const double bottom = std::min({annotation.points[0].price, annotation.points[1].price, annotation.points[2].price});
      const int topPoint = std::abs(annotation.points[1].price - top) <= std::abs(annotation.points[2].price - top) ? 1 : 2;
      const int bottomPoint = topPoint == 1 ? 2 : 1;
      if (handle == 0) {
        annotation.points[topPoint].price = std::max(current.price, annotation.points[0].price);
      } else if (handle == 1) {
        annotation.points[bottomPoint].price = std::min(current.price, annotation.points[0].price);
      } else if (handle == 2) {
        const double minEnd = annotation.points[0].index + 1.0;
        const double nextIndex = std::max(minEnd, current.index);
        annotation.points[1].index = nextIndex;
        annotation.points[2].index = nextIndex;
      }
      return;
    }
    if (annotationDragMode_ == AnnotationDragMode::ResizePoint && annotationDragPoint_ >= 0 && annotationDragPoint_ < annotation.points.size()) {
      annotation.points = annotationDragOriginal_;
      annotation.points[annotationDragPoint_] = current;
      if ((annotation.tool == AnnotationTool::LongBlock || annotation.tool == AnnotationTool::ShortBlock) && annotation.points.size() >= 3 && annotationDragPoint_ > 0) {
        annotation.points[1].index = current.index;
        annotation.points[2].index = current.index;
      }
      return;
    }
    if (annotationDragMode_ == AnnotationDragMode::Move) {
      const double di = current.index - annotationDragStart_.index;
      const double dp = current.price - annotationDragStart_.price;
      annotation.points = annotationDragOriginal_;
      for (AnnotationPoint &point : annotation.points) {
        point.index += di;
        point.price += dp;
      }
    }
  }

  void finishAnnotationDrag() {
    if (selectedAnnotation_ >= 0 && selectedAnnotation_ < manualAnnotations_.size()) rememberAnnotationStyle(manualAnnotations_[selectedAnnotation_]);
    annotationDragMode_ = AnnotationDragMode::None;
    annotationDragPoint_ = -1;
    annotationDragOriginal_.clear();
    syncAnnotationStyleToolbar();
    update();
  }

  void showAnnotationContextMenu(const QPointF &pos, const QPoint &globalPos) {
    const int index = annotationAt(pos);
    if (index < 0) return;
    selectedAnnotation_ = index;
    QMenu menu;
    QAction *style = menu.addAction("样式设置");
    QAction *remove = menu.addAction("删除");
    QAction *chosen = menu.exec(globalPos);
    if (chosen == remove) deleteSelectedAnnotation();
    else if (chosen == style) editSelectedAnnotationStyle();
    update();
  }

  void editSelectedAnnotationStyle() {
    if (selectedAnnotation_ < 0 || selectedAnnotation_ >= manualAnnotations_.size()) return;
    ManualAnnotation &annotation = manualAnnotations_[selectedAnnotation_];
    QDialog dialog(this);
    dialog.setWindowTitle("标记样式");
    auto *layout = new QFormLayout(&dialog);
    auto *lineButton = new QPushButton(annotation.style.line.name());
    auto *fillButton = new QPushButton(annotation.style.fill.name());
    auto *profitButton = new QPushButton(annotation.style.profit.name());
    auto *lossButton = new QPushButton(annotation.style.loss.name());
    auto *width = new QSpinBox;
    width->setRange(1, 8);
    width->setValue(annotation.style.lineWidth);
    auto *opacity = new QSlider(Qt::Horizontal);
    opacity->setRange(10, 220);
    opacity->setValue(annotation.style.opacity);
    QColor line = annotation.style.line;
    QColor fill = annotation.style.fill;
    QColor profit = annotation.style.profit;
    QColor loss = annotation.style.loss;
    connect(lineButton, &QPushButton::clicked, &dialog, [&] {
      const QColor color = QColorDialog::getColor(line, &dialog, "线条颜色");
      if (color.isValid()) {
        line = color;
        lineButton->setText(color.name());
      }
    });
    connect(fillButton, &QPushButton::clicked, &dialog, [&] {
      const QColor color = QColorDialog::getColor(fill, &dialog, "背景颜色");
      if (color.isValid()) {
        fill = color;
        fillButton->setText(color.name());
      }
    });
    connect(profitButton, &QPushButton::clicked, &dialog, [&] {
      const QColor color = QColorDialog::getColor(profit, &dialog, "盈利区颜色");
      if (color.isValid()) {
        profit = color;
        profitButton->setText(color.name());
      }
    });
    connect(lossButton, &QPushButton::clicked, &dialog, [&] {
      const QColor color = QColorDialog::getColor(loss, &dialog, "亏损区颜色");
      if (color.isValid()) {
        loss = color;
        lossButton->setText(color.name());
      }
    });
    layout->addRow("线条颜色", lineButton);
    if (annotation.tool == AnnotationTool::LongBlock || annotation.tool == AnnotationTool::ShortBlock) {
      layout->addRow("盈利区颜色", profitButton);
      layout->addRow("亏损区颜色", lossButton);
    } else {
      layout->addRow("背景颜色", fillButton);
    }
    layout->addRow("线条粗细", width);
    layout->addRow("透明度", opacity);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
      annotation.style.line = line;
      annotation.style.fill = fill;
      annotation.style.profit = profit;
      annotation.style.loss = loss;
      annotation.style.lineWidth = width->value();
      annotation.style.opacity = opacity->value();
      rememberAnnotationStyle(annotation);
      syncAnnotationStyleToolbar();
      update();
    }
  }

  enum class StyleColorRole { Line, Fill, Profit, Loss };

  QPushButton *styleColorButton(const QString &tooltip) {
    auto *button = new QPushButton;
    button->setFixedSize(24, 24);
    button->setToolTip(tooltip);
    return button;
  }

  void buildAnnotationStyleToolbar() {
    annotationStyleToolbar_ = new QFrame(this);
    annotationStyleToolbar_->setObjectName("annotationFloatingToolbar");
    auto *layout = new QHBoxLayout(annotationStyleToolbar_);
    layout->setContentsMargins(8, 5, 8, 5);
    layout->setSpacing(6);
    lineColorButton_ = styleColorButton("线条颜色");
    fillColorButton_ = styleColorButton("背景颜色");
    profitColorButton_ = styleColorButton("盈利区颜色");
    lossColorButton_ = styleColorButton("亏损区颜色");
    lineWidthSpin_ = new QSpinBox;
    lineWidthSpin_->setRange(1, 8);
    lineWidthSpin_->setFixedWidth(52);
    layout->addWidget(lineColorButton_);
    layout->addWidget(fillColorButton_);
    layout->addWidget(profitColorButton_);
    layout->addWidget(lossColorButton_);
    layout->addWidget(lineWidthSpin_);
    annotationStyleToolbar_->hide();

    connect(lineColorButton_, &QPushButton::clicked, this, [this] { chooseSelectedColor(StyleColorRole::Line); });
    connect(fillColorButton_, &QPushButton::clicked, this, [this] { chooseSelectedColor(StyleColorRole::Fill); });
    connect(profitColorButton_, &QPushButton::clicked, this, [this] { chooseSelectedColor(StyleColorRole::Profit); });
    connect(lossColorButton_, &QPushButton::clicked, this, [this] { chooseSelectedColor(StyleColorRole::Loss); });
    connect(lineWidthSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
      if (selectedAnnotation_ < 0 || selectedAnnotation_ >= manualAnnotations_.size() || syncingStyleToolbar_) return;
      manualAnnotations_[selectedAnnotation_].style.lineWidth = value;
      rememberAnnotationStyle(manualAnnotations_[selectedAnnotation_]);
      update();
    });
  }

  void updateColorButton(QPushButton *button, const QColor &color) {
    if (!button) return;
    button->setStyleSheet(QString("QPushButton { background: %1; border: 1px solid rgba(230,226,211,90); border-radius: 2px; }").arg(color.name()));
  }

  void syncAnnotationStyleToolbar() {
    if (!annotationStyleToolbar_) return;
    const bool hasSelection = selectedAnnotation_ >= 0 && selectedAnnotation_ < manualAnnotations_.size();
    annotationStyleToolbar_->setVisible(hasSelection);
    if (!hasSelection) return;
    const ManualAnnotation &annotation = manualAnnotations_[selectedAnnotation_];
    const bool positionBlock = annotation.tool == AnnotationTool::LongBlock || annotation.tool == AnnotationTool::ShortBlock;
    fillColorButton_->setVisible(!positionBlock);
    profitColorButton_->setVisible(positionBlock);
    lossColorButton_->setVisible(positionBlock);
    updateColorButton(lineColorButton_, annotation.style.line);
    updateColorButton(fillColorButton_, annotation.style.fill);
    updateColorButton(profitColorButton_, annotation.style.profit);
    updateColorButton(lossColorButton_, annotation.style.loss);
    syncingStyleToolbar_ = true;
    lineWidthSpin_->setValue(annotation.style.lineWidth);
    syncingStyleToolbar_ = false;
    const int w = positionBlock ? 178 : 118;
    annotationStyleToolbar_->setFixedSize(w, 34);
    annotationStyleToolbar_->move(std::max(54, (width() - w) / 2), 18);
    annotationStyleToolbar_->raise();
  }

  void chooseSelectedColor(StyleColorRole role) {
    if (selectedAnnotation_ < 0 || selectedAnnotation_ >= manualAnnotations_.size()) return;
    ManualAnnotation &annotation = manualAnnotations_[selectedAnnotation_];
    QColor current = annotation.style.line;
    if (role == StyleColorRole::Fill) current = annotation.style.fill;
    if (role == StyleColorRole::Profit) current = annotation.style.profit;
    if (role == StyleColorRole::Loss) current = annotation.style.loss;
    const QColor color = QColorDialog::getColor(current, this, "选择颜色");
    if (!color.isValid()) return;
    if (role == StyleColorRole::Line) annotation.style.line = color;
    if (role == StyleColorRole::Fill) annotation.style.fill = color;
    if (role == StyleColorRole::Profit) annotation.style.profit = color;
    if (role == StyleColorRole::Loss) annotation.style.loss = color;
    rememberAnnotationStyle(annotation);
    syncAnnotationStyleToolbar();
    update();
  }

  QPointF pointAt(int index, double price, double minPrice, double maxPrice) const {
    return pointAtIndex(index, price, minPrice, maxPrice);
  }

  QPointF pointAtIndex(double index, double price, double minPrice, double maxPrice) const {
    const QRectF r = plotRect();
    const double x = r.left() + (index - visibleStart_ + 0.5) * barStep();
    return QPointF(x, yFor(price, minPrice, maxPrice));
  }

  qint64 timeForIndex(double index) const {
    if (candles_.isEmpty()) return 0;
    const qint64 step = barIntervalMs();
    if (index <= 0) return candles_.first().ms + static_cast<qint64>(index * step);
    if (index >= candleCount() - 1) return candles_.last().ms + static_cast<qint64>((index - (candleCount() - 1)) * step);
    const int left = std::clamp(static_cast<int>(std::floor(index)), 0, candleCount() - 1);
    const int right = std::min(candleCount() - 1, left + 1);
    const double t = index - left;
    return candles_[left].ms + static_cast<qint64>((candles_[right].ms - candles_[left].ms) * t);
  }

  int visibleEnd() const {
    return std::min(candleCount(), static_cast<int>(std::ceil(visibleStart_ + visibleCount_)));
  }

  void requestMoreIfNeeded() {
    if (loadingOlderRequested_ || candles_.isEmpty()) return;
    if (visibleStart_ < 50) {
      loadingOlderRequested_ = true;
      emit olderCandlesRequested(candles_.first().ms - 1);
      QTimer::singleShot(900, this, [this] { loadingOlderRequested_ = false; });
    }
  }

  void emitOverlayRange() {
    if (candles_.isEmpty()) return;
    const int start = std::clamp(static_cast<int>(std::floor(visibleStart_)), 0, candleCount() - 1);
    const int end = std::clamp(visibleEnd() - 1, 0, candleCount() - 1);
    emit overlayRangeChanged(candles_[start].ms, candles_[end].ms + 20 * barIntervalMs());
  }

  void emitVisibleRange() {
    if (candles_.isEmpty()) {
      emit visibleRangeChanged(0, 0, -1, -1);
      return;
    }
    const int start = std::clamp(static_cast<int>(std::floor(visibleStart_)), 0, candleCount() - 1);
    const int end = std::clamp(visibleEnd() - 1, 0, candleCount() - 1);
    emit visibleRangeChanged(candles_[start].ms, candles_[end].ms, start, end);
  }

  void scheduleRepaint() {
    if (repaintScheduled_) return;
    repaintScheduled_ = true;
    QTimer::singleShot(0, this, [this] {
      repaintScheduled_ = false;
      update();
    });
  }

  void rebuildIndicatorsNow() {
    indicatorRebuildScheduled_ = false;
    indicatorEngine_.rebuild(candles_);
  }

  void scheduleIndicatorRebuild() {
    if (indicatorRebuildScheduled_) return;
    indicatorRebuildScheduled_ = true;
    QTimer::singleShot(80, this, [this] {
      indicatorRebuildScheduled_ = false;
      indicatorEngine_.rebuild(candles_);
      scheduleRepaint();
    });
  }

  double layerNumber(const QJsonObject &object, const QString &name, double fallback = std::numeric_limits<double>::quiet_NaN()) const {
    const QJsonValue value = object.value(name);
    if (value.isDouble()) return value.toDouble(fallback);
    if (value.isString()) {
      bool ok = false;
      const double number = value.toString().toDouble(&ok);
      if (ok) return number;
    }
    return fallback;
  }

  qint64 layerTime(const QJsonObject &object, const QString &name, qint64 fallback = 0) const {
    const double value = layerNumber(object, name, std::numeric_limits<double>::quiet_NaN());
    return std::isfinite(value) ? static_cast<qint64>(value) : fallback;
  }

  QColor layerColor(const QJsonObject &object, const QString &name, const QColor &fallback) const {
    const QString text = object.value(name).toString().trimmed();
    if (text.isEmpty()) return fallback;
    const QColor color(text);
    return color.isValid() ? color : fallback;
  }

  QColor withOpacity(QColor color, double opacity) const {
    color.setAlpha(std::clamp(static_cast<int>(std::round(std::clamp(opacity, 0.0, 1.0) * 255.0)), 0, 255));
    return color;
  }

  OverlayStyle parseLayerStyle(const QJsonObject &object) const {
    OverlayStyle style;
    style.stroke = layerColor(object, "stroke", dark_ ? QColor("#e6e2d3") : QColor("#374151"));
    style.fill = layerColor(object, "fill", QColor("#6ed7f6"));
    style.textColor = layerColor(object, "textColor", dark_ ? QColor("#f4efe3") : QColor("#131916"));
    style.profitFill = layerColor(object, "profitFill", QColor("#20c997"));
    style.lossFill = layerColor(object, "lossFill", QColor("#ef5f78"));
    style.entryLine = layerColor(object, "entryLine", dark_ ? QColor("#f4efe8") : QColor("#374151"));
    style.slLine = layerColor(object, "slLine", QColor("#ef4444"));
    style.tpLine = layerColor(object, "tpLine", QColor("#10b981"));
    style.strokeWidth = std::clamp(static_cast<int>(layerNumber(object, "strokeWidth", 1)), 1, 8);
    style.strokeOpacity = std::clamp(layerNumber(object, "strokeOpacity", 1.0), 0.0, 1.0);
    style.fillOpacity = std::clamp(layerNumber(object, "fillOpacity", 0.35), 0.0, 1.0);
    style.fontSize = std::clamp(static_cast<int>(layerNumber(object, "fontSize", 10)), 8, 22);
    const QJsonArray dash = object.value("dash").toArray();
    for (const QJsonValue &value : dash) {
      const double segment = value.toDouble(0);
      if (segment > 0) style.dash.push_back(segment);
    }
    return style;
  }

  QPen layerPen(const OverlayStyle &style) const {
    QPen pen(withOpacity(style.stroke, style.strokeOpacity), style.strokeWidth);
    if (!style.dash.isEmpty()) pen.setDashPattern(style.dash);
    return pen;
  }

  OverlayPoint parseLayerPoint(const QJsonObject &object) const {
    return {layerTime(object, "time"), layerNumber(object, "price")};
  }

  void parseOverlayLayers(const QJsonArray &layers) {
    QSet<QString> seenGroups;
    parsedLayers_.clear();
    layerGroupOrder_.clear();
    for (const QJsonValue &value : layers) {
      const QJsonObject object = value.toObject();
      OverlayLayer layer;
      layer.id = object.value("id").toString().trimmed();
      if (layer.id.isEmpty()) {
        layer.id = QString("layer-%1").arg(qHash(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact))));
      }
      layer.type = object.value("type").toString().trimmed().toLower();
      layer.group = object.value("group").toString("Default").trimmed();
      if (layer.group.isEmpty()) layer.group = "Default";
      layer.visible = !object.contains("visible") || object.value("visible").toBool(true);
      layer.zIndex = static_cast<int>(layerNumber(object, "zIndex", 0));
      layer.from = layerTime(object, "from");
      layer.to = layerTime(object, "to");
      layer.entryTime = layerTime(object, "entryTime", layer.from);
      layer.exitTime = layerTime(object, "exitTime", layer.to);
      layer.price = layerNumber(object, "price");
      layer.top = layerNumber(object, "top");
      layer.bottom = layerNumber(object, "bottom");
      layer.entry = layerNumber(object, "entry");
      layer.sl = layerNumber(object, "sl");
      layer.tp1 = layerNumber(object, "tp1");
      layer.tp2 = layerNumber(object, "tp2");
      layer.quantity = layerNumber(object, "quantity");
      layer.amount = layerNumber(object, "amount");
      layer.side = object.value("side").toString().trimmed().toLower();
      layer.shape = object.value("shape").toString("circle").trimmed().toLower();
      layer.text = object.value("text").toString();
      layer.anchor = object.value("anchor").toString("center").trimmed().toLower();
      layer.data = object.value("data").toObject();
      layer.style = parseLayerStyle(object.value("style").toObject());
      for (const QJsonValue &pointValue : object.value("points").toArray()) {
        const OverlayPoint point = parseLayerPoint(pointValue.toObject());
        if (point.time > 0 && std::isfinite(point.price)) layer.points.push_back(point);
      }
      if (layer.type.isEmpty()) continue;
      if (!seenGroups.contains(layer.group)) {
        seenGroups.insert(layer.group);
        layerGroupOrder_.push_back(layer.group);
        if (!layerGroupVisible_.contains(layer.group)) layerGroupVisible_.insert(layer.group, true);
      }
      parsedLayers_.push_back(layer);
    }
    std::sort(parsedLayers_.begin(), parsedLayers_.end(), [](const OverlayLayer &a, const OverlayLayer &b) {
      if (a.zIndex == b.zIndex) return a.id < b.id;
      return a.zIndex < b.zIndex;
    });
  }

  void paintOverlays(QPainter &p, double minPrice, double maxPrice) {
    positionHitboxes_.clear();
    if (candles_.isEmpty()) return;
    p.save();
    p.setClipRect(plotRect());
    p.setFont(uiFont(10, QFont::Medium));
    if (!parsedLayers_.isEmpty()) {
      paintGenericLayers(p, minPrice, maxPrice);
      p.restore();
      return;
    }
    if (parsedOverlayEvents_.isEmpty()) {
      p.restore();
      return;
    }

    for (const ParsedOverlayEvent &parsed : parsedOverlayEvents_) {
      const QJsonObject event = parsed.event;
      const QString type = event.value("eventType").toString();
      const QJsonObject payload = parsed.payload;
      if (rangeVisible_ && type == "RANGE_BOUNDARY_UPDATED") drawRangeEvent(p, payload, minPrice, maxPrice);
      if (rangeVisible_ && type == "RANGE_BOUNDARY_TOUCHED") drawRangeTouchEvent(p, payload, event, minPrice, maxPrice);
      if (nVisible_ && type == "HIGH_N_DETECTED") drawNEvent(p, payload.value("n").toObject(), QColor(240, 182, 79, 220), "N", minPrice, maxPrice);
      if (ninVisible_ && type == "HIGH_N_IN_DETECTED") {
        drawNEvent(p, payload.value("base_n").toObject(), QColor(230, 226, 211, 120), "Base", minPrice, maxPrice);
        drawNEvent(p, payload.value("signal_n").toObject(), QColor(39, 212, 177, 220), "N-IN", minPrice, maxPrice);
      }
    }
    if (orderVisible_) drawPositions(p, minPrice, maxPrice);
    if (ifvgVisible_) {
      QSet<QString> drawnIfvgs;
      for (const ParsedOverlayEvent &parsed : parsedOverlayEvents_) {
        const QJsonObject event = parsed.event;
        const QString type = event.value("eventType").toString();
        if (type == "ENTRY_SIGNAL_OPEN_SENT" || type == "POSITION_OPEN_FILLED") {
          drawIfvgEvent(p, parsed.payload, event, drawnIfvgs, minPrice, maxPrice);
        }
      }
    }
    p.restore();
  }

  void paintIndicators(QPainter &p, double minPrice, double maxPrice) {
    if (candles_.isEmpty()) return;
    const int start = std::max(0, static_cast<int>(std::floor(visibleStart_)) - 2);
    const int end = std::min(candleCount(), static_cast<int>(std::ceil(visibleStart_ + visibleCount_)) + 2);

    p.save();
    p.setClipRect(plotRect());
    p.setRenderHint(QPainter::Antialiasing, true);

    for (const IndicatorBox &boxShape : indicatorEngine_.boxes()) {
      if (boxShape.to < start || boxShape.from >= end) continue;
      const int from = std::clamp(boxShape.from, 0, candleCount() - 1);
      const int to = std::clamp(boxShape.to, 0, candleCount() - 1);
      QRectF rect(pointAtIndex(from, boxShape.top, minPrice, maxPrice), pointAtIndex(to, boxShape.bottom, minPrice, maxPrice));
      rect = rect.normalized();
      p.fillRect(rect, boxShape.fill);
      p.setBrush(Qt::NoBrush);
      p.setPen(QPen(boxShape.border, 1));
      p.drawRect(rect);
    }

    for (const IndicatorLine &lineShape : indicatorEngine_.lines()) {
      if (lineShape.to < start || lineShape.from >= end) continue;
      p.setPen(QPen(lineShape.color, lineShape.width));
      p.drawLine(pointAtIndex(lineShape.from, lineShape.y1, minPrice, maxPrice),
                 pointAtIndex(lineShape.to, lineShape.y2, minPrice, maxPrice));
    }

    for (const IndicatorPlot &plot : indicatorEngine_.plots()) {
      if (plot.values.isEmpty()) continue;
      p.setPen(QPen(plot.color, plot.width));
      QPainterPath path;
      bool active = false;
      const int plotEnd = std::min(end, static_cast<int>(plot.values.size()));
      for (int i = std::max(0, start); i < plotEnd; ++i) {
        const double value = plot.values[i];
        if (!std::isfinite(value)) {
          active = false;
          continue;
        }
        const QPointF point = pointAtIndex(i, value, minPrice, maxPrice);
        if (!active) {
          path.moveTo(point);
          active = true;
        } else {
          path.lineTo(point);
        }
      }
      p.drawPath(path);
    }

    const QVector<IndicatorMarker> &markers = indicatorEngine_.markers();
    for (const IndicatorMarker &marker : markers) {
      if (marker.index < start || marker.index >= end || marker.index >= candleCount()) continue;
      if (!std::isfinite(marker.price)) continue;
      const double direction = marker.location == "belowbar" ? 1.0 : -1.0;
      const QPointF anchor = pointAtIndex(marker.index, marker.price, minPrice, maxPrice) + QPointF(0, direction * marker.pixelOffset);
      const QColor fill(marker.color.red(), marker.color.green(), marker.color.blue(), 230);
      const QColor border(147, 197, 253, 235);
      p.setPen(QPen(marker.shape.startsWith("triangle") ? fill : border, 1.25));
      p.setBrush(fill);
      if (marker.shape == "triangle_up") {
        QPolygonF triangle;
        triangle << QPointF(anchor.x(), anchor.y() - 6) << QPointF(anchor.x() - 6, anchor.y() + 5) << QPointF(anchor.x() + 6, anchor.y() + 5);
        p.drawPolygon(triangle);
      } else if (marker.shape == "triangle_down") {
        QPolygonF triangle;
        triangle << QPointF(anchor.x(), anchor.y() + 6) << QPointF(anchor.x() - 6, anchor.y() - 5) << QPointF(anchor.x() + 6, anchor.y() - 5);
        p.drawPolygon(triangle);
      } else {
        p.drawEllipse(anchor, 4.4, 4.4);
      }
    }

    p.setFont(uiFont(10, QFont::Medium));
    for (const IndicatorLabel &labelShape : indicatorEngine_.labels()) {
      if (labelShape.index < start || labelShape.index >= end || labelShape.index >= candleCount()) continue;
      if (!std::isfinite(labelShape.price) || labelShape.text.isEmpty()) continue;
      const QPointF anchor = pointAtIndex(labelShape.index, labelShape.price, minPrice, maxPrice);
      const QFontMetrics fm(p.font());
      QRectF box(anchor.x() + 6, anchor.y() - 18, fm.horizontalAdvance(labelShape.text) + 10, 18);
      p.setPen(QPen(labelShape.color, 1));
      p.setBrush(dark_ ? QColor(11, 16, 15, 226) : QColor(255, 253, 247, 235));
      p.drawRect(box);
      p.setPen(labelShape.color);
      p.drawText(box, Qt::AlignCenter, labelShape.text);
    }
    p.restore();
  }

  QJsonObject parsePayload(const QJsonObject &event) const {
    const QString raw = event.value("payloadJson").toString();
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    return doc.isObject() ? doc.object() : QJsonObject{};
  }

  QPointF pointAtTime(qint64 ms, double price, double minPrice, double maxPrice) const {
    if (candles_.isEmpty()) return {};
    return pointAtIndex(indexForTime(ms), price, minPrice, maxPrice);
  }

  double indexForTime(qint64 ms) const {
    if (candles_.isEmpty()) return -1.0;
    if (ms <= candles_.first().ms) return 0.0;
    if (ms > candles_.last().ms) {
      const qint64 step = barIntervalMs();
      return candleCount() - 1 + double(ms - candles_.last().ms) / std::max<qint64>(1, step);
    }
    const auto it = std::lower_bound(candles_.begin(), candles_.end(), ms, [](const Candle &c, qint64 t) {
      return c.ms < t;
    });
    if (it == candles_.end()) return candleCount() - 1;
    const int next = static_cast<int>(std::distance(candles_.begin(), it));
    if (it->ms == ms || next == 0) return next;
    const int prev = next - 1;
    const qint64 span = std::max<qint64>(1, candles_[next].ms - candles_[prev].ms);
    return prev + double(ms - candles_[prev].ms) / span;
  }

  int indexAtTime(qint64 ms) const {
    if (candles_.isEmpty()) return -1;
    if (ms > candles_.last().ms) {
      const qint64 step = barIntervalMs();
      return candleCount() - 1 + static_cast<int>(std::ceil(double(ms - candles_.last().ms) / std::max<qint64>(1, step)));
    }
    const auto it = std::lower_bound(candles_.begin(), candles_.end(), ms, [](const Candle &c, qint64 t) {
      return c.ms < t;
    });
    int index = 0;
    if (it == candles_.end()) index = candleCount() - 1;
    else index = static_cast<int>(std::distance(candles_.begin(), it));
    return std::clamp(index, 0, candleCount() - 1);
  }

  qint64 barIntervalMs() const {
    if (candles_.size() >= 2) {
      const qint64 diff = candles_.last().ms - candles_[candles_.size() - 2].ms;
      if (diff > 0) return diff;
    }
    return 60000;
  }

  qint64 jsonMs(const QJsonValue &value) const {
    return static_cast<qint64>(value.toDouble());
  }

  QString formatChartTime(qint64 ms, const QString &format) const {
    QTimeZone zone(timeZoneId_);
    if (!zone.isValid()) zone = QTimeZone::systemTimeZone();
    return QDateTime::fromMSecsSinceEpoch(ms, zone).toString(format);
  }

  QString rangeKey(const QString &side, qint64 start) const {
    if (side.isEmpty() || start <= 0) return {};
    return side + ":" + QString::number(start);
  }

  bool timeWindowVisible(qint64 start, qint64 end) const {
    if (candles_.isEmpty() || start <= 0 || end <= 0) return false;
    const double a = indexForTime(start);
    const double b = indexForTime(end);
    const double left = std::min(a, b);
    const double right = std::max(a, b);
    return right >= visibleStart_ - 2.0 && left <= visibleStart_ + visibleCount_ + 2.0;
  }

  void drawRangeEvent(QPainter &p, const QJsonObject &payload, double minPrice, double maxPrice) {
    const QJsonObject point = payload.value("point").toObject();
    const double price = point.value("price").toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!std::isfinite(price)) return;
    const QString side = point.value("side").toString();
    const qint64 logicalStart = jsonMs(point.value("time"));
    const qint64 start = jsonMs(point.value("display_time").isUndefined() ? point.value("time") : point.value("display_time"));
    const qint64 end = rangeEndMs(side, logicalStart);
    if (!timeWindowVisible(start, end)) return;
    if (price < minPrice || price > maxPrice) return;
    const QPointF a = pointAtTime(start, price, minPrice, maxPrice);
    const QPointF b = pointAtTime(end, price, minPrice, maxPrice);
    const QColor color = side == "HIGH" ? QColor(245, 158, 11, 199) : QColor(56, 189, 248, 199);
    p.setPen(QPen(color, 1, Qt::DashLine));
    p.drawLine(a, b);
    p.setPen(color);
    p.drawText(a + QPointF(4, -5), side.isEmpty() ? "Range" : side);
  }

  qint64 rangeEndMs(const QString &side, qint64 start) const {
    const qint64 end = rangeEndByKey_.value(rangeKey(side, start), 0);
    if (end > 0) return end;
    const qint64 base = candles_.isEmpty() ? start : candles_.last().ms;
    return base + 10 * barIntervalMs();
  }

  void drawRangeTouchEvent(QPainter &p, const QJsonObject &payload, const QJsonObject &event, double minPrice, double maxPrice) {
    const bool upperBreak = payload.value("side").toString() == "UPPER";
    const qint64 time = jsonMs(event.value("eventTime"));
    const int index = indexAtTime(time);
    if (index < 0 || index >= candleCount()) return;
    const double price = upperBreak ? candles_[index].high : candles_[index].low;
    const QPointF pos = pointAt(index, price, minPrice, maxPrice) + QPointF(0, upperBreak ? -14 : 14);
    QPolygonF arrow;
    if (upperBreak) {
      arrow << QPointF(pos.x(), pos.y() - 8) << QPointF(pos.x() - 5, pos.y() + 2) << QPointF(pos.x() + 5, pos.y() + 2);
    } else {
      arrow << QPointF(pos.x(), pos.y() + 8) << QPointF(pos.x() - 5, pos.y() - 2) << QPointF(pos.x() + 5, pos.y() - 2);
    }
    const QColor color = upperBreak ? up() : down();
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawPolygon(arrow);
    p.setPen(color);
    p.drawText(pos + QPointF(8, upperBreak ? 3 : 5), upperBreak ? "Break ↑" : "Break ↓");
  }

  void drawNEvent(QPainter &p, const QJsonObject &n, const QColor &color, const QString &label, double minPrice, double maxPrice) {
    if (n.isEmpty()) return;
    QVector<QPointF> pts;
    auto add = [&](const QString &timeKey, const QString &fallbackTimeKey, const QString &priceKey) {
      const qint64 t = jsonMs(n.value(timeKey).isUndefined() ? n.value(fallbackTimeKey) : n.value(timeKey));
      const double price = n.value(priceKey).toDouble(std::numeric_limits<double>::quiet_NaN());
      if (t > 0 && std::isfinite(price)) pts << pointAtTime(t, price, minPrice, maxPrice);
    };
    add("anchor_display_time", "anchor_time", "anchor_price");
    add("turning_display_time", "turning_time", "turning_price");
    add("retrace_display_time", "retrace_time", "retrace_price");
    add("breakout_time", "breakout_time", "breakout_close");
    if (pts.size() < 2) return;
    p.setPen(QPen(color, 2));
    p.drawPolyline(pts.constData(), pts.size());
    p.drawText(pts.last() + QPointF(6, -6), label);
  }

  void drawIfvgEvent(QPainter &p, const QJsonObject &payload, const QJsonObject &event, QSet<QString> &drawn, double minPrice, double maxPrice) {
    const QJsonObject signal = payload.value("entry_signal").toObject();
    QJsonObject fvg = signal.value("fvg").toObject();
    if (signal.value("type").toString() != "IFVG" && payload.contains("stop_ifvg")) {
      fvg = payload.value("stop_ifvg").toObject();
    }
    if (signal.value("type").toString() != "IFVG" && fvg.isEmpty()) return;
    if (fvg.isEmpty()) return;
    const double top = fvg.value("top").toDouble(std::numeric_limits<double>::quiet_NaN());
    const double bottom = fvg.value("bottom").toDouble(std::numeric_limits<double>::quiet_NaN());
    const QJsonObject k1 = fvg.value("k1").toObject();
    qint64 start = jsonMs(k1.value("open_time"));
    if (start <= 0) start = jsonMs(fvg.value("k1_time").isUndefined() ? fvg.value("create_time") : fvg.value("k1_time"));
    const qint64 end = jsonMs(fvg.value("ifvg_time").isUndefined() ? signal.value("time") : fvg.value("ifvg_time"));
    if (!std::isfinite(top) || !std::isfinite(bottom) || start <= 0 || end <= 0) return;
    const QString key = QString("%1:%2:%3:%4").arg(start).arg(end).arg(top, 0, 'g', 14).arg(bottom, 0, 'g', 14);
    if (drawn.contains(key)) return;
    drawn.insert(key);
    const double startIndex = indexForTime(start);
    const double endIndex = std::max(startIndex, static_cast<double>(indexAtTime(end) - 1));
    QRectF box(pointAtIndex(startIndex, top, minPrice, maxPrice), pointAtIndex(endIndex, bottom, minPrice, maxPrice));
    box = box.normalized();
    p.save();
    p.fillRect(box, QColor(110, 215, 246, 128));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(147, 197, 253, 210), 1));
    p.drawRect(box);
    p.drawText(box.topLeft() + QPointF(4, -4), "iFVG");
    p.restore();
  }

  void drawPositionEvent(QPainter &p, const QJsonObject &payload, const QJsonObject &event, double minPrice, double maxPrice) {
    const qint64 start = jsonMs(payload.value("entry_time").isUndefined() ? event.value("eventTime") : payload.value("entry_time"));
    const qint64 end = positionEndMs(payload, start);
    const double entry = payload.value("exec_price").toDouble(payload.value("entry").toDouble(event.value("price").toDouble(std::numeric_limits<double>::quiet_NaN())));
    const double sl = payload.value("sl").toDouble(std::numeric_limits<double>::quiet_NaN());
    const double tp1 = payload.value("tp1").toDouble(std::numeric_limits<double>::quiet_NaN());
    if (start <= 0 || !std::isfinite(entry)) return;
    if (std::isfinite(tp1)) {
      QRectF reward(pointAtTime(start, tp1, minPrice, maxPrice), pointAtTime(end, entry, minPrice, maxPrice));
      p.fillRect(reward.normalized(), QColor(32, 201, 151, 60));
    }
    if (std::isfinite(sl)) {
      QRectF danger(pointAtTime(start, entry, minPrice, maxPrice), pointAtTime(end, sl, minPrice, maxPrice));
      p.fillRect(danger.normalized(), QColor(239, 95, 120, 62));
    }
    const QPointF a = pointAtTime(start, entry, minPrice, maxPrice);
    const QPointF b = pointAtTime(end, entry, minPrice, maxPrice);
    p.setPen(QPen(dark_ ? QColor(244, 239, 227, 225) : QColor(55, 65, 81, 245), 1));
    p.drawLine(a, b);
    p.drawText(b + QPointF(6, 4), "Entry");
    if (markerVisible_) {
      QPolygonF arrow;
      arrow << QPointF(a.x(), a.y() - 10) << QPointF(a.x() - 5, a.y()) << QPointF(a.x() + 5, a.y());
      p.setBrush(up());
      p.setPen(Qt::NoPen);
      p.drawPolygon(arrow);
    }
  }

  struct PositionPartial {
    qint64 time = 0;
    double exitPrice = std::numeric_limits<double>::quiet_NaN();
    double pnl = std::numeric_limits<double>::quiet_NaN();
  };

  struct PositionShape {
    QString key;
    QString direction;
    qint64 entryTime = 0;
    double entry = std::numeric_limits<double>::quiet_NaN();
    double sl = std::numeric_limits<double>::quiet_NaN();
    double tp1 = std::numeric_limits<double>::quiet_NaN();
    double quantity = std::numeric_limits<double>::quiet_NaN();
    QString signalType;
    QString backgroundType;
    QString backgroundDirection;
    QVector<PositionPartial> partials;
    bool opened = false;
    bool closed = false;
    qint64 closeTime = 0;
    double exitPrice = std::numeric_limits<double>::quiet_NaN();
    double totalPnl = std::numeric_limits<double>::quiet_NaN();
  };

  QString positionKey(const QJsonObject &payload, qint64 entryTime) const {
    const qint64 positionId = static_cast<qint64>(payload.value("position_id").toDouble(0));
    return positionId > 0 ? QString("p:%1").arg(positionId) : QString("t:%1").arg(entryTime);
  }

  QString positionSignalKey(const QString &direction, qint64 entryTime) const {
    return QString("%1:%2").arg(direction).arg(entryTime);
  }

  void drawPositions(QPainter &p, double minPrice, double maxPrice) {
    struct SentEntry {
      QString signalType;
      QString backgroundType;
      QString backgroundDirection;
    };
    QHash<QString, SentEntry> sentEntries;
    for (const ParsedOverlayEvent &parsed : parsedOverlayEvents_) {
      const QJsonObject event = parsed.event;
      if (event.value("eventType").toString() != "ENTRY_SIGNAL_OPEN_SENT") continue;
      const QJsonObject payload = parsed.payload;
      const qint64 entryTime = jsonMs(event.value("eventTime").isUndefined() ? payload.value("entry_time") : event.value("eventTime"));
      if (entryTime <= 0) continue;
      const QJsonObject signal = payload.value("entry_signal").toObject();
      const QJsonObject background = payload.value("background").toObject();
      sentEntries.insert(positionSignalKey(event.value("direction").toString(), entryTime), {
        signal.value("type").toString(),
        background.value("type").toString(),
        background.value("direction").toString()
      });
    }

    QHash<QString, PositionShape> positions;
    for (const ParsedOverlayEvent &parsed : parsedOverlayEvents_) {
      const QJsonObject event = parsed.event;
      const QString type = event.value("eventType").toString();
      const QJsonObject payload = parsed.payload;
      if (type == "POSITION_OPEN_FILLED") {
        const qint64 entryTime = jsonMs(payload.value("entry_time").isUndefined() ? event.value("eventTime") : payload.value("entry_time"));
        const QString key = positionKey(payload, entryTime);
        PositionShape position = positions.value(key);
        position.key = key;
        position.direction = event.value("direction").toString();
        position.entryTime = entryTime;
        position.entry = payload.value("exec_price").toDouble(payload.value("entry").toDouble(event.value("price").toDouble(std::numeric_limits<double>::quiet_NaN())));
        position.sl = payload.value("sl").toDouble(std::numeric_limits<double>::quiet_NaN());
        position.tp1 = payload.value("tp1").toDouble(std::numeric_limits<double>::quiet_NaN());
        position.quantity = payload.value("quantity").toDouble(std::numeric_limits<double>::quiet_NaN());
        position.opened = true;
        const QJsonObject signal = payload.value("entry_signal").toObject();
        const QJsonObject background = payload.value("background").toObject();
        const SentEntry sent = sentEntries.value(positionSignalKey(position.direction, entryTime));
        position.signalType = signal.value("type").toString(position.signalType.isEmpty() ? sent.signalType : position.signalType);
        position.backgroundType = background.value("type").toString(position.backgroundType.isEmpty() ? sent.backgroundType : position.backgroundType);
        position.backgroundDirection = background.value("direction").toString(position.backgroundDirection.isEmpty() ? sent.backgroundDirection : position.backgroundDirection);
        positions.insert(key, position);
      } else if (type == "POSITION_PARTIAL_CLOSED" || type == "POSITION_CLOSED") {
        const qint64 entryTime = jsonMs(payload.value("entry_time"));
        if (entryTime <= 0) continue;
        const QString key = positionKey(payload, entryTime);
        PositionShape position = positions.value(key);
        position.key = key;
        position.direction = event.value("direction").toString(position.direction);
        position.entryTime = entryTime;
        if (!std::isfinite(position.entry)) position.entry = payload.value("entry").toDouble(std::numeric_limits<double>::quiet_NaN());
        if (type == "POSITION_PARTIAL_CLOSED") {
          PositionPartial partial;
          partial.time = jsonMs(payload.value("exit_time").isUndefined() ? event.value("eventTime") : payload.value("exit_time"));
          partial.exitPrice = payload.value("exit_price").toDouble(event.value("price").toDouble(std::numeric_limits<double>::quiet_NaN()));
          partial.pnl = payload.value("close_pnl").toDouble(std::numeric_limits<double>::quiet_NaN());
          position.partials.push_back(partial);
        } else {
          position.closed = true;
          position.closeTime = jsonMs(payload.value("exit_time").isUndefined() ? event.value("eventTime") : payload.value("exit_time"));
          position.exitPrice = payload.value("exit_price").toDouble(event.value("price").toDouble(std::numeric_limits<double>::quiet_NaN()));
          position.totalPnl = payload.value("total_pnl").toDouble(payload.value("close_pnl").toDouble(std::numeric_limits<double>::quiet_NaN()));
        }
        positions.insert(key, position);
      }
    }

    for (const PositionShape &position : std::as_const(positions)) {
      if (!position.opened) continue;
      drawPositionShape(p, position, minPrice, maxPrice);
    }
  }

  void drawPositionShape(QPainter &p, const PositionShape &position, double minPrice, double maxPrice) {
    if (!std::isfinite(position.entry) || position.entryTime <= 0) return;
    const bool isLong = position.direction == "LONG";
    qint64 end = position.closed && position.closeTime > 0 ? position.closeTime : ((candles_.isEmpty() ? position.entryTime : candles_.last().ms) + 5 * barIntervalMs());
    const double entry = position.entry;
    const double sl = position.sl;
    double firstPartialExit = std::numeric_limits<double>::quiet_NaN();
    if (!position.partials.isEmpty()) firstPartialExit = position.partials.first().exitPrice;
    const double tp2 = std::isfinite(position.exitPrice) && (isLong ? position.exitPrice > entry : position.exitPrice < entry) ? position.exitPrice : std::numeric_limits<double>::quiet_NaN();
    const double profitBoundary = std::isfinite(tp2) ? tp2 : firstPartialExit;
    const bool hasTp1 = std::isfinite(firstPartialExit) && (isLong ? firstPartialExit > entry : firstPartialExit < entry);
    const bool tp1BetterThanTp2 = hasTp1 && std::isfinite(position.exitPrice) && (isLong ? firstPartialExit > position.exitPrice : firstPartialExit < position.exitPrice);
    registerPositionHitbox(position, end, entry, sl, firstPartialExit, position.exitPrice, minPrice, maxPrice);

    if (std::isfinite(sl) && (isLong ? sl < entry : sl > entry)) {
      const bool emphasized = std::isfinite(position.totalPnl) && position.totalPnl < 0;
      drawPositionArea(p, position.entryTime, end, entry, sl, false, emphasized, minPrice, maxPrice);
      drawLineAt(p, position.entryTime, end, sl, QColor(239, 95, 120, 225), Qt::SolidLine, minPrice, maxPrice);
    }
    if (std::isfinite(profitBoundary) && (isLong ? profitBoundary > entry : profitBoundary < entry)) {
      const bool emphasized = std::isfinite(position.totalPnl) && position.totalPnl >= 0;
      drawPositionArea(p, position.entryTime, end, entry, profitBoundary, true, emphasized, minPrice, maxPrice);
    }
    if (tp1BetterThanTp2) {
      drawRangeArea(p, position.entryTime, end, firstPartialExit, position.exitPrice, QColor(148, 163, 184, 77), minPrice, maxPrice);
    }
    if (hasTp1) {
      drawLineAt(p, position.entryTime, end, firstPartialExit, QColor(32, 201, 151, 210), Qt::DashLine, minPrice, maxPrice);
    }
    if (std::isfinite(tp2) && (!std::isfinite(firstPartialExit) || std::abs(tp2 - firstPartialExit) > 1e-9)) {
      drawLineAt(p, position.entryTime, end, tp2, QColor(32, 201, 151, 242), Qt::SolidLine, minPrice, maxPrice);
    }
    drawLineAt(p, position.entryTime, end, entry, dark_ ? QColor(244, 239, 232, 225) : QColor(55, 65, 81, 245), Qt::SolidLine, minPrice, maxPrice);

    if (markerVisible_) {
      const QString qty = std::isfinite(position.quantity) ? QString(" %1").arg(QString::number(position.quantity, 'g', 4)) : "";
      drawEntryMarker(p, position.entryTime, entry, isLong, isLong ? up() : down(), QString("%1%2").arg(isLong ? "L" : "S", qty), minPrice, maxPrice);
      for (const PositionPartial &partial : position.partials) {
        drawCircleMarker(p, partial.time, partial.exitPrice, QColor("#f0b64f"), "TP1", minPrice, maxPrice);
      }
      if (position.closed) {
        const QColor color = std::isfinite(position.totalPnl) && position.totalPnl >= 0 ? up() : down();
        drawBadgeMarker(p, position.closeTime, std::isfinite(position.exitPrice) ? position.exitPrice : entry, !isLong, color, "Exit", minPrice, maxPrice);
      }
    }
  }

  void drawLineAt(QPainter &p, qint64 start, qint64 end, double price, const QColor &color, Qt::PenStyle style, double minPrice, double maxPrice) {
    if (!std::isfinite(price)) return;
    p.setPen(QPen(color, 1, style));
    p.drawLine(pointAtTime(start, price, minPrice, maxPrice), pointAtTime(end, price, minPrice, maxPrice));
  }

  void drawPositionArea(QPainter &p, qint64 start, qint64 end, double entry, double boundary, bool profitable, bool emphasized, double minPrice, double maxPrice) {
    const int strong = emphasized ? (profitable ? 138 : 133) : (profitable ? 66 : 61);
    const int soft = emphasized ? 61 : 26;
    const QColor base = profitable ? QColor(32, 201, 151) : QColor(239, 95, 120);
    QRectF area(pointAtTime(start, entry, minPrice, maxPrice), pointAtTime(end, boundary, minPrice, maxPrice));
    area = area.normalized();
    QLinearGradient gradient(area.topLeft(), area.bottomLeft());
    gradient.setColorAt(0.0, QColor(base.red(), base.green(), base.blue(), boundary > entry ? strong : soft));
    gradient.setColorAt(1.0, QColor(base.red(), base.green(), base.blue(), boundary > entry ? soft : strong));
    p.fillRect(area, gradient);
  }

  void drawRangeArea(QPainter &p, qint64 start, qint64 end, double a, double b, const QColor &fill, double minPrice, double maxPrice) {
    if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a - b) < 1e-9) return;
    QRectF area(pointAtTime(start, a, minPrice, maxPrice), pointAtTime(end, b, minPrice, maxPrice));
    p.fillRect(area.normalized(), fill);
  }

  void drawEntryMarker(QPainter &p, qint64 time, double price, bool isLong, const QColor &color, const QString &label, double minPrice, double maxPrice) {
    drawBadgeMarker(p, time, price, isLong, color, label, minPrice, maxPrice);
  }

  void drawBadgeMarker(QPainter &p, qint64 time, double price, bool below, const QColor &color, const QString &label, double minPrice, double maxPrice) {
    const QPointF anchor = pointAtTime(time, price, minPrice, maxPrice);
    QFont f = uiFont(10, QFont::DemiBold);
    p.setFont(f);
    const QFontMetrics fm(f);
    const QSize textSize = fm.size(Qt::TextSingleLine, label);
    QRectF badge(anchor.x() - textSize.width() / 2.0 - 7, anchor.y() + (below ? 10 : -28), textSize.width() + 14, 18);
    p.setPen(QPen(color, 1));
    p.setBrush(dark_ ? QColor(11, 16, 15, 230) : QColor(255, 253, 247, 235));
    p.drawRect(badge);
    p.setPen(color);
    p.drawText(badge, Qt::AlignCenter, label);
  }

  void drawMarker(QPainter &p, qint64 time, double price, bool upArrow, const QColor &color, const QString &label, double minPrice, double maxPrice) {
    const QPointF pos = pointAtTime(time, price, minPrice, maxPrice) + QPointF(0, upArrow ? 14 : -14);
    QPolygonF arrow;
    if (upArrow) {
      arrow << QPointF(pos.x(), pos.y() - 9) << QPointF(pos.x() - 5, pos.y() + 1) << QPointF(pos.x() + 5, pos.y() + 1);
    } else {
      arrow << QPointF(pos.x(), pos.y() + 9) << QPointF(pos.x() - 5, pos.y() - 1) << QPointF(pos.x() + 5, pos.y() - 1);
    }
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawPolygon(arrow);
    p.setPen(color);
    p.drawText(pos + QPointF(8, 4), label);
  }

  void drawCircleMarker(QPainter &p, qint64 time, double price, const QColor &color, const QString &label, double minPrice, double maxPrice) {
    if (!std::isfinite(price)) return;
    const QPointF pos = pointAtTime(time, price, minPrice, maxPrice);
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawEllipse(pos, 4, 4);
    p.setPen(color);
    p.drawText(pos + QPointF(8, 4), label);
  }

  void paintLatestPriceLine(QPainter &p, double minPrice, double maxPrice) {
    if (candles_.isEmpty()) return;
    const Candle &latest = candles_.last();
    const QRectF r = plotRect();
    const double y = yFor(latest.close, minPrice, maxPrice);
    const QColor color = latest.close >= latest.open ? up() : down();
    p.setPen(QPen(QColor(color.red(), color.green(), color.blue(), 185), 1, Qt::DashLine));
    p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    drawAxisTag(p, QRectF(r.right() + 6, y - 9, 66, 18), QString::number(latest.close, 'f', 2), color);
  }

  qint64 positionEndMs(const QJsonObject &openPayload, qint64 entryTime) const {
    const double positionId = openPayload.value("position_id").toDouble(0);
    for (const ParsedOverlayEvent &parsed : parsedOverlayEvents_) {
      const QJsonObject event = parsed.event;
      if (event.value("eventType").toString() != "POSITION_CLOSED") continue;
      const QJsonObject payload = parsed.payload;
      const double closePositionId = payload.value("position_id").toDouble(0);
      const qint64 closeEntryTime = jsonMs(payload.value("entry_time"));
      if ((positionId > 0 && closePositionId == positionId) || (positionId <= 0 && closeEntryTime == entryTime)) {
        const qint64 exit = jsonMs(payload.value("exit_time").isUndefined() ? event.value("eventTime") : payload.value("exit_time"));
        if (exit > 0) return exit;
      }
    }
    const qint64 base = candles_.isEmpty() ? entryTime : candles_.last().ms;
    return base + 5 * barIntervalMs();
  }

  void paintLayerHints(QPainter &p) {
    p.setFont(uiFont(12, QFont::Medium));
    int y = 28;
    auto row = [&](bool visible, const QString &name) {
      p.setPen(visible ? text() : QColor(muted().red(), muted().green(), muted().blue(), 115));
      drawEyeIcon(p, QRectF(14, y - 14, 20, 16), visible);
      p.drawText(QRectF(40, y - 16, 170, 20), Qt::AlignVCenter | Qt::AlignLeft, name);
      y += 24;
    };
    row(rangeVisible_, "震荡区间");
    row(nVisible_, "N字结构");
    row(ninVisible_, "N-IN / N-InverseK");
    row(ifvgVisible_, "iFVG");
    row(indicatorEngine_.fvgCircleSettings().enabled, "FVG Circle");
    for (const IndicatorScript &script : indicatorEngine_.scripts()) {
      row(script.enabled, script.name);
    }
    for (const QString &group : layerGroupOrder_) {
      row(layerGroupVisible_.value(group, true), group);
    }
    row(orderVisible_, "持仓视图");
    row(markerVisible_, "订单标注");
  }

  void drawEyeIcon(QPainter &p, const QRectF &rect, bool visible) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor color = visible ? text() : QColor(muted().red(), muted().green(), muted().blue(), 125);
    QPainterPath eye;
    eye.moveTo(rect.left() + 1, rect.center().y());
    eye.cubicTo(rect.left() + 5, rect.top() + 1, rect.right() - 5, rect.top() + 1, rect.right() - 1, rect.center().y());
    eye.cubicTo(rect.right() - 5, rect.bottom() - 1, rect.left() + 5, rect.bottom() - 1, rect.left() + 1, rect.center().y());
    p.setPen(QPen(color, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawPath(eye);
    if (visible) {
      p.setBrush(color);
      p.setPen(Qt::NoPen);
      p.drawEllipse(rect.center(), 3.2, 3.2);
    } else {
      p.setPen(QPen(color, 1.6));
      p.drawLine(rect.bottomLeft() + QPointF(2, -1), rect.topRight() + QPointF(-2, 1));
    }
    p.restore();
  }

  struct PositionHitbox {
    QRectF rect;
    QString key;
    QString direction;
    qint64 startMs = 0;
    qint64 endMs = 0;
    double entryY = std::numeric_limits<double>::quiet_NaN();
    double entry = std::numeric_limits<double>::quiet_NaN();
    double sl = std::numeric_limits<double>::quiet_NaN();
    double oneRtp = std::numeric_limits<double>::quiet_NaN();
    double tp1R = std::numeric_limits<double>::quiet_NaN();
    double tp2R = std::numeric_limits<double>::quiet_NaN();
    double pnl = std::numeric_limits<double>::quiet_NaN();
    double quantity = std::numeric_limits<double>::quiet_NaN();
    QString signalType;
    QString backgroundType;
    QString backgroundDirection;
    QJsonObject copyData;
  };

  void registerGenericPositionHitbox(const OverlayLayer &layer, qint64 start, qint64 end, double minPrice, double maxPrice) {
    QVector<double> prices;
    auto add = [&](double value) {
      if (std::isfinite(value)) prices.push_back(value);
    };
    add(layer.entry);
    add(layer.sl);
    add(layer.tp1);
    add(layer.tp2);
    if (prices.size() < 2) return;
    const auto [minIt, maxIt] = std::minmax_element(prices.begin(), prices.end());
    QRectF rect(pointAtTime(start, *minIt, minPrice, maxPrice), pointAtTime(end, *maxIt, minPrice, maxPrice));
    rect = rect.normalized();
    PositionHitbox hitbox;
    hitbox.rect = rect.adjusted(-2, -2, 2, 2);
    hitbox.key = layer.id;
    hitbox.direction = layer.side.toUpper();
    hitbox.startMs = start;
    hitbox.endMs = end;
    hitbox.entryY = pointAtTime(start, layer.entry, minPrice, maxPrice).y();
    hitbox.entry = layer.entry;
    hitbox.sl = layer.sl;
    const double risk = std::isfinite(layer.sl) ? std::abs(layer.entry - layer.sl) : std::numeric_limits<double>::quiet_NaN();
    if (std::isfinite(risk) && risk > 0) hitbox.oneRtp = layer.side == "short" ? layer.entry - risk : layer.entry + risk;
    hitbox.tp1R = std::isfinite(layer.tp1) && risk > 0 ? std::abs(layer.tp1 - layer.entry) / risk : std::numeric_limits<double>::quiet_NaN();
    hitbox.tp2R = std::isfinite(layer.tp2) && risk > 0 ? std::abs(layer.tp2 - layer.entry) / risk : std::numeric_limits<double>::quiet_NaN();
    hitbox.quantity = layer.quantity;
    hitbox.copyData = layer.data;
    positionHitboxes_.push_back(hitbox);
  }

  void registerPositionHitbox(const PositionShape &position, qint64 end, double entry, double sl, double tp1, double exitPrice, double minPrice, double maxPrice) {
    QVector<double> prices;
    auto add = [&](double value) {
      if (std::isfinite(value)) prices.push_back(value);
    };
    add(entry);
    add(sl);
    add(tp1);
    add(exitPrice);
    if (prices.size() < 2) return;
    const auto [minIt, maxIt] = std::minmax_element(prices.begin(), prices.end());
    QRectF rect(pointAtTime(position.entryTime, *minIt, minPrice, maxPrice), pointAtTime(end, *maxIt, minPrice, maxPrice));
    rect = rect.normalized();
    const double risk = std::isfinite(sl) ? std::abs(entry - sl) : std::numeric_limits<double>::quiet_NaN();
    PositionHitbox hitbox;
    hitbox.rect = rect.adjusted(-2, -2, 2, 2);
    hitbox.key = position.key;
    hitbox.direction = position.direction;
    hitbox.startMs = position.entryTime;
    hitbox.endMs = end;
    hitbox.entryY = pointAtTime(position.entryTime, entry, minPrice, maxPrice).y();
    hitbox.entry = entry;
    hitbox.sl = sl;
    if (std::isfinite(sl)) {
      const double risk = std::abs(entry - sl);
      if (risk > 0) hitbox.oneRtp = position.direction == "LONG" ? entry + risk : entry - risk;
    }
    hitbox.tp1R = std::isfinite(tp1) && risk > 0 ? std::abs(tp1 - entry) / risk : std::numeric_limits<double>::quiet_NaN();
    hitbox.tp2R = std::isfinite(exitPrice) && risk > 0 ? std::abs(exitPrice - entry) / risk : std::numeric_limits<double>::quiet_NaN();
    hitbox.pnl = position.totalPnl;
    hitbox.quantity = position.quantity;
    hitbox.signalType = position.signalType;
    hitbox.backgroundType = position.backgroundType;
    hitbox.backgroundDirection = position.backgroundDirection;
    positionHitboxes_.push_back(hitbox);
  }

  int positionHitboxAt(const QPointF &point) const {
    int bestIndex = -1;
    double bestScore = std::numeric_limits<double>::infinity();
    for (int i = positionHitboxes_.size() - 1; i >= 0; --i) {
      const PositionHitbox &hitbox = positionHitboxes_[i];
      if (!hitbox.rect.contains(point)) continue;
      const double entryY = std::isfinite(hitbox.entryY) ? hitbox.entryY : hitbox.rect.center().y();
      const double priceScore = std::abs(point.y() - entryY);
      const double timeScore = std::abs(point.x() - hitbox.rect.left()) / std::max(1.0, hitbox.rect.width()) * 0.01;
      const double score = priceScore + timeScore;
      if (score < bestScore || (std::abs(score - bestScore) < 1e-9 && (bestIndex < 0 || hitbox.startMs > positionHitboxes_[bestIndex].startMs))) {
        bestIndex = i;
        bestScore = score;
      }
    }
    return bestIndex;
  }

  QString jsonNumber(double value) const {
    return std::isfinite(value) ? QString::number(value, 'g', 15) : "null";
  }

  QString positionCopyJson(const PositionHitbox &hitbox) const {
    if (!hitbox.copyData.isEmpty()) {
      return QString::fromUtf8(QJsonDocument(hitbox.copyData).toJson(QJsonDocument::Indented)).trimmed();
    }
    QStringList candleLines;
    for (const Candle &candle : candles_) {
      if (candle.ms < hitbox.startMs || candle.ms > hitbox.endMs) continue;
      candleLines << QString(
        "        {\n"
        "            \"o\": %1,\n"
        "            \"h\": %2,\n"
        "            \"l\": %3,\n"
        "            \"c\": %4\n"
        "        }"
      ).arg(jsonNumber(candle.open), jsonNumber(candle.high), jsonNumber(candle.low), jsonNumber(candle.close));
    }
    return QString(
      "{\n"
      "    \"position_key\": \"%1\",\n"
      "    \"direction\": \"%2\",\n"
      "    \"start_time\": %3,\n"
      "    \"end_time\": %4,\n"
      "    \"entry\": %5,\n"
      "    \"sl\": %6,\n"
      "    \"1r_tp\": %7,\n"
      "    \"kline\": [\n"
      "%8\n"
      "    ]\n"
      "}"
    ).arg(
      hitbox.key,
      hitbox.direction,
      QString::number(hitbox.startMs),
      QString::number(hitbox.endMs),
      jsonNumber(hitbox.entry),
      jsonNumber(hitbox.sl),
      jsonNumber(hitbox.oneRtp),
      candleLines.join(",\n")
    );
  }

  bool toggleLayerAt(const QPointF &point) {
    const int customCount = indicatorEngine_.scripts().size();
    const int layerGroupCount = layerGroupOrder_.size();
    const int totalRows = 7 + customCount + layerGroupCount;
    if (point.x() < 8 || point.x() > 230 || point.y() < 10 || point.y() > 14 + totalRows * 24) return false;
    const int row = static_cast<int>((point.y() - 14) / 24);
    switch (row) {
      case 0: rangeVisible_ = !rangeVisible_; return true;
      case 1: nVisible_ = !nVisible_; return true;
      case 2: ninVisible_ = !ninVisible_; return true;
      case 3: ifvgVisible_ = !ifvgVisible_; return true;
      case 4: {
        FvgCircleSettings settings = indicatorEngine_.fvgCircleSettings();
        settings.enabled = !settings.enabled;
        indicatorEngine_.setFvgCircleSettings(settings);
        rebuildIndicatorsNow();
        emit fvgCircleVisibilityChanged(settings.enabled);
        return true;
      }
      default: {
        const int scriptIndex = row - 5;
        if (scriptIndex >= 0 && scriptIndex < indicatorEngine_.scripts().size()) {
          const IndicatorScript script = indicatorEngine_.scripts()[scriptIndex];
          indicatorEngine_.setScriptEnabled(script.id, !script.enabled);
          rebuildIndicatorsNow();
          emit customIndicatorVisibilityChanged(script.id, !script.enabled);
          return true;
        }
        const int groupIndex = row - 5 - customCount;
        if (groupIndex >= 0 && groupIndex < layerGroupOrder_.size()) {
          const QString group = layerGroupOrder_[groupIndex];
          layerGroupVisible_.insert(group, !layerGroupVisible_.value(group, true));
          return true;
        }
        if (row == 5 + customCount + layerGroupCount) {
          orderVisible_ = !orderVisible_;
          return true;
        }
        if (row == 6 + customCount + layerGroupCount) {
          markerVisible_ = !markerVisible_;
          return true;
        }
        return false;
      }
    }
  }

  void paintCrosshair(QPainter &p) {
    if (hoveredIndex_ < 0 || hoveredIndex_ >= candleCount()) return;
    double minPrice, maxPrice;
    visibleRange(minPrice, maxPrice);
    const QRectF r = plotRect();
    const double x = r.left() + (hoveredIndex_ - visibleStart_ + 0.5) * barStep();
    p.setPen(QPen(dark_ ? QColor(244, 239, 227, 70) : QColor(19, 25, 22, 70), 1, Qt::DashLine));
    p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    if (hasMouse_ && r.contains(mousePos_)) {
      p.drawLine(QPointF(r.left(), mousePos_.y()), QPointF(r.right(), mousePos_.y()));
      drawAxisTag(p, QRectF(r.right() + 6, mousePos_.y() - 9, 66, 18), QString::number(priceForY(mousePos_.y(), minPrice, maxPrice), 'f', 2), QColor("#f0b64f"));
      const QString time = formatChartTime(candles_[hoveredIndex_].ms, "MM-dd HH:mm");
      drawAxisTag(p, QRectF(x - 48, r.bottom() + 6, 96, 18), time, QColor("#f0b64f"));
    }
  }

  void drawAxisTag(QPainter &p, const QRectF &rect, const QString &text, const QColor &color) {
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRect(rect);
    p.setPen(QColor("#111813"));
    p.setFont(uiFont(11, QFont::Medium));
    p.drawText(rect, Qt::AlignCenter, text);
  }

  void updateCursor() {
    if (annotationTool_ == AnnotationTool::None) unsetCursor();
    else setCursor(Qt::CrossCursor);
  }

  QString formatCompact(double value, int precision = 2) const {
    return std::isfinite(value) ? QString::number(value, 'f', precision) : "--";
  }

  QString formatBackground(const PositionHitbox &hitbox) const {
    if (hitbox.backgroundType.isEmpty() && hitbox.backgroundDirection.isEmpty()) return "--";
    return hitbox.backgroundType + (hitbox.backgroundDirection.isEmpty() ? "" : " / " + hitbox.backgroundDirection);
  }

  void paintPositionTooltip(QPainter &p) {
    if (!hasMouse_ || hoveredPositionIndex_ < 0 || hoveredPositionIndex_ >= positionHitboxes_.size()) return;
    const PositionHitbox &hitbox = positionHitboxes_[hoveredPositionIndex_];
    QRectF panel(mousePos_.x() + 14, mousePos_.y() + 14, 226, 132);
    panel.moveLeft(std::min(panel.left(), width() - panel.width() - 10));
    panel.moveTop(std::max(10.0, std::min(panel.top(), height() - panel.height() - 10.0)));
    p.setPen(QPen(dark_ ? QColor(230, 226, 211, 46) : QColor(23, 31, 27, 48), 1));
    p.setBrush(dark_ ? QColor(12, 17, 15, 235) : QColor(255, 253, 247, 238));
    p.drawRect(panel);

    p.setFont(uiFont(11, QFont::DemiBold));
    const QColor sideColor = hitbox.direction == "LONG" ? up() : down();
    QRectF side(panel.left() + 12, panel.top() + 10, 56, 22);
    p.setPen(QPen(sideColor, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(side);
    p.drawText(side, Qt::AlignCenter, hitbox.direction.isEmpty() ? "--" : hitbox.direction);
    p.setPen(text());
    p.drawText(QRectF(panel.right() - 86, panel.top() + 10, 74, 22), Qt::AlignRight | Qt::AlignVCenter, formatCompact(hitbox.quantity, 4));
    p.setPen(QPen(dark_ ? QColor(230, 226, 211, 36) : QColor(23, 31, 27, 36), 1));
    p.drawLine(QPointF(panel.left() + 12, panel.top() + 42), QPointF(panel.right() - 12, panel.top() + 42));

    p.setFont(uiFont(10, QFont::Medium));
    const QStringList labels{"Background", "Signal", "Entry", "TP1 R", "TP2 R", "PNL"};
    const QStringList values{
      formatBackground(hitbox),
      hitbox.signalType.isEmpty() ? "--" : hitbox.signalType,
      formatCompact(hitbox.entry),
      std::isfinite(hitbox.tp1R) ? QString("%1R").arg(formatCompact(hitbox.tp1R)) : "--",
      std::isfinite(hitbox.tp2R) ? QString("%1R").arg(formatCompact(hitbox.tp2R)) : "--",
      std::isfinite(hitbox.pnl) ? QString::number(hitbox.pnl, 'f', 2) : "--"
    };
    for (int i = 0; i < labels.size(); ++i) {
      const double y = panel.top() + 55 + i * 12;
      p.setPen(muted());
      p.drawText(QPointF(panel.left() + 12, y), labels[i]);
      p.setPen(i == 5 && std::isfinite(hitbox.pnl) ? (hitbox.pnl >= 0 ? up() : down()) : text());
      p.drawText(QRectF(panel.left() + 92, y - 10, panel.width() - 104, 13), Qt::AlignRight | Qt::AlignVCenter, values[i]);
    }
  }

  void paintOhlcSketch(QPainter &p) {
    if (hoveredIndex_ < 0 || hoveredIndex_ >= candleCount()) return;
    const Candle &c = candles_[hoveredIndex_];
    const bool green = c.close >= c.open;
    const QColor color = green ? up() : down();
    QRectF panel(width() - 284, 18, 176, 150);
    p.setPen(QPen(dark_ ? QColor(230, 226, 211, 34) : QColor(23, 31, 27, 34), 1));
    p.setBrush(dark_ ? QColor(14, 19, 17, 178) : QColor(255, 253, 247, 210));
    p.drawRect(panel);

    p.setFont(uiFont(11, QFont::DemiBold));
    p.setPen(QColor("#f0b64f"));
    p.drawText(panel.adjusted(8, 8, 0, 0), "OHLC 示意图");

    const double top = panel.top() + 30;
    const double bodyTop = panel.top() + 58;
    const double bodyBottom = panel.top() + 96;
    const double bottom = panel.bottom() - 16;
    const double cx = panel.center().x();
    p.setPen(QPen(color, 2));
    p.drawLine(QPointF(cx, top), QPointF(cx, bottom));
    QRectF body(cx - 11, bodyTop, 22, bodyBottom - bodyTop);
    p.fillRect(body, color);
    p.drawRect(body);

    p.setFont(numberFont(12, QFont::Medium));
    p.setPen(muted());
    p.drawText(QPointF(panel.left() + 10, green ? bodyBottom + 3 : bodyTop + 3), QString("O %1").arg(c.open));
    p.setPen(color);
    p.drawText(QPointF(panel.left() + 10, green ? bodyTop + 3 : bodyBottom + 3), QString("C %1").arg(c.close));
    p.setPen(QColor("#f0b64f"));
    p.drawText(QPointF(panel.right() - 72, top + 3), QString("H %1").arg(c.high));
    p.setPen(QColor("#72d9f7"));
    p.drawText(QPointF(panel.right() - 72, bottom + 3), QString("L %1").arg(c.low));
  }

  QVector<Candle> candles_;
  QJsonArray overlayEvents_;
  struct ParsedOverlayEvent {
    QJsonObject event;
    QJsonObject payload;
  };
  QVector<ParsedOverlayEvent> parsedOverlayEvents_;
  QVector<OverlayLayer> parsedLayers_;
  QStringList layerGroupOrder_;
  QHash<QString, bool> layerGroupVisible_;
  QHash<QString, qint64> rangeEndByKey_;
  IndicatorEngine indicatorEngine_;
  QString messageText_;
  QByteArray timeZoneId_ = QTimeZone::systemTimeZoneId();
  bool dark_ = true;
  bool rangeVisible_ = true;
  bool nVisible_ = true;
  bool ninVisible_ = true;
  bool ifvgVisible_ = true;
  bool orderVisible_ = true;
  bool markerVisible_ = true;
  double visibleStart_ = 0.0;
  int visibleCount_ = 160;
  int rightOffsetBars_ = 40;
  int hoveredIndex_ = -1;
  int hoveredPositionIndex_ = -1;
  bool hasMouse_ = false;
  QPointF mousePos_;
  bool dragging_ = false;
  bool xAxisScaling_ = false;
  bool yAxisScaling_ = false;
  bool loadingOlderRequested_ = false;
  QPoint dragStart_;
  double dragVisibleStart_ = 0.0;
  int axisVisibleCount_ = 160;
  double axisVisibleStart_ = 0.0;
  double axisAnchorIndex_ = 0.0;
  double axisAnchorLocalX_ = 0.0;
  double manualPriceScale_ = 1.0;
  double axisPriceScale_ = 1.0;
  double manualPriceOffset_ = 0.0;
  double dragPriceOffset_ = 0.0;
  double dragPriceRange_ = 1.0;
  bool repaintScheduled_ = false;
  bool indicatorRebuildScheduled_ = false;
  QVector<PositionHitbox> positionHitboxes_;
  AnnotationTool annotationTool_ = AnnotationTool::None;
  bool magnetEnabled_ = true;
  bool drawingAnnotation_ = false;
  AnnotationPoint draftStart_;
  AnnotationPoint draftPoint_;
  QVector<AnnotationPoint> draftPolyline_;
  QVector<ManualAnnotation> manualAnnotations_;
  int selectedAnnotation_ = -1;
  AnnotationDragMode annotationDragMode_ = AnnotationDragMode::None;
  int annotationDragPoint_ = -1;
  static constexpr int positionAnnotationHandleBase_ = 1000;
  AnnotationPoint annotationDragStart_;
  QVector<AnnotationPoint> annotationDragOriginal_;
  QHash<int, AnnotationStyle> annotationDefaultStyles_;
  QFrame *annotationStyleToolbar_ = nullptr;
  QPushButton *lineColorButton_ = nullptr;
  QPushButton *fillColorButton_ = nullptr;
  QPushButton *profitColorButton_ = nullptr;
  QPushButton *lossColorButton_ = nullptr;
  QSpinBox *lineWidthSpin_ = nullptr;
  bool syncingStyleToolbar_ = false;
};

bool ChartWidget::showPositionContextMenu(const QPointF &pos, const QPoint &globalPos) {
  const int index = positionHitboxAt(pos);
  if (index < 0 || index >= positionHitboxes_.size()) return false;
  QMenu menu;
  QAction *copy = menu.addAction("复制区块信息");
  QAction *chosen = menu.exec(globalPos);
  if (chosen == copy) {
    QApplication::clipboard()->setText(positionCopyJson(positionHitboxes_[index]));
    QToolTip::showText(globalPos, "已复制区块信息", this);
  }
  return true;
}

class CandleClient : public QObject {
  Q_OBJECT

public:
  explicit CandleClient(QObject *parent = nullptr) : QObject(parent) {
    backendBase_ = normalizeBase(envOrDefault("Q4J_BACKEND_URL", "http://127.0.0.1:8080"));
    wsBase_ = normalizeBase(envOrDefault("Q4J_WS_BASE", wsFromHttp(backendBase_)));
    liveWatchdog_.setInterval(30000);
    connect(&liveWatchdog_, &QTimer::timeout, this, [this] {
      if (!realtimeEnabled_ || socket_.state() != QAbstractSocket::ConnectedState) return;
      if (lastRealtimeMessageMs_ <= 0 || QDateTime::currentMSecsSinceEpoch() - lastRealtimeMessageMs_ > realtimeTimeoutMs()) {
        emitDebugLog("WS watchdog: connected but no recent valid candle");
        emit statusChanged("实时无数据", false);
      }
    });
    connect(&socket_, &QWebSocket::connected, this, [this] {
      lastRealtimeMessageMs_ = 0;
      emitDebugLog("WS connected");
      emit statusChanged("等待实时", false);
      liveWatchdog_.start();
    });
    connect(&socket_, &QWebSocket::disconnected, this, [this] {
      liveWatchdog_.stop();
      lastRealtimeMessageMs_ = 0;
      emitDebugLog(QString("WS disconnected, closeCode=%1, reason=%2")
        .arg(static_cast<int>(socket_.closeCode()))
        .arg(socket_.closeReason()));
      emit statusChanged("断开", false);
    });
    connect(&socket_, &QWebSocket::textMessageReceived, this, &CandleClient::onSocketMessage);
    connect(&socket_, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, [this] {
      emitDebugLog(QString("WS error: %1").arg(socket_.errorString()));
      emit statusChanged("断开", false);
    });
  }

  QString backendBase() const { return backendBase_; }
  QString wsBase() const { return wsBase_; }
  bool realtimeEnabled() const { return realtimeEnabled_; }
  bool hasConfiguredBackend() const { return !backendBase_.trimmed().isEmpty(); }

  void configureBackend(const QString &backendBase, const QString &wsBase, bool realtimeEnabled) {
    backendBase_ = normalizeBase(backendBase.trimmed());
    wsBase_ = normalizeBase(wsBase.trimmed().isEmpty() ? wsFromHttp(backendBase_) : wsBase.trimmed());
    realtimeEnabled_ = realtimeEnabled;
    emitDebugLog(QString("Backend configured: http=%1, ws=%2, realtime=%3")
      .arg(backendBase_, wsBase_, realtimeEnabled_ ? "true" : "false"));
    if (!realtimeEnabled_) {
      socket_.close();
      liveWatchdog_.stop();
      emit statusChanged("实时关闭", false);
    } else {
      socket_.close();
    }
  }

  void load(const QString &symbol, const QString &interval, const QString &strategyName, const QString &higherInterval, const QString &lowerInterval) {
    symbol_ = symbol.trimmed();
    interval_ = interval.trimmed().toLower();
    strategyName_ = normalizedStrategyName(strategyName);
    higherInterval_ = higherInterval.trimmed().toLower();
    lowerInterval_ = lowerInterval.trimmed().toLower();
    knownStartMs_ = 0;
    knownEndMs_ = 0;
    overlayLoadedStartMs_ = 0;
    overlayLoadedEndMs_ = 0;
    overlayEvents_ = {};
    overlayLayerDocument_ = {};
    if (backendBase_.isEmpty()) {
      emit statusChanged("后端未配置", false);
      return;
    }
    emit statusChanged("加载中", false);

    const qint64 end = QDateTime::currentMSecsSinceEpoch();
    const qint64 start = end - intervalMs(interval_) * 320;
    QUrl url(backendBase_ + "/api/candles");
    QUrlQuery query;
    query.addQueryItem("symbol", symbol_);
    query.addQueryItem("interval", interval_);
    query.addQueryItem("startTime", QString::number(start));
    query.addQueryItem("endTime", QString::number(end));
    url.setQuery(query);

    QNetworkReply *reply = network_.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
      const QByteArray body = reply->readAll();
      reply->deleteLater();
      if (reply->error() != QNetworkReply::NoError) {
        emit statusChanged("加载失败", false);
        emit loadFailed(replyErrorMessage(reply, body));
        return;
      }
      const QJsonDocument doc = QJsonDocument::fromJson(body);
      if (doc.isObject()) {
        const QString message = doc.object().value("message").toString();
        emit statusChanged("加载失败", false);
        emit loadFailed(message.isEmpty() ? "响应格式错误" : message);
        socket_.close();
        return;
      }
      if (!doc.isArray()) {
        emit statusChanged("加载失败", false);
        emit loadFailed("响应格式错误");
        return;
      }
      QVector<Candle> candles;
      for (const QJsonValue &value : doc.array()) {
        const QJsonObject obj = value.toObject();
        const Candle candle = parseCandle(obj);
        if (isValidCandle(candle)) candles.push_back(candle);
      }
      updateKnownRange(candles);
      emit errorMessage({});
      emit candlesLoaded(candles);
      fetchOverlayEvents(knownStartMs_, knownEndMs_ + intervalMs(interval_) * 20);
      if (realtimeEnabled_) connectSocket();
      else emit statusChanged("实时关闭", false);
    });
  }

  void loadOlder(qint64 beforeMs) {
    if (loadingOlder_ || backendBase_.isEmpty() || symbol_.isEmpty() || interval_.isEmpty()) return;
    loadingOlder_ = true;
    const qint64 end = beforeMs;
    const qint64 start = std::max<qint64>(0, end - intervalMs(interval_) * 500);
    QUrl url(backendBase_ + "/api/candles");
    QUrlQuery query;
    query.addQueryItem("symbol", symbol_);
    query.addQueryItem("interval", interval_);
    query.addQueryItem("startTime", QString::number(start));
    query.addQueryItem("endTime", QString::number(end));
    url.setQuery(query);
    QNetworkReply *reply = network_.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, start, end] {
      const QByteArray body = reply->readAll();
      reply->deleteLater();
      loadingOlder_ = false;
      if (reply->error() != QNetworkReply::NoError) {
        emit statusChanged("历史加载失败", false);
        emit errorMessage(replyErrorMessage(reply, body));
        return;
      }
      const QJsonDocument doc = QJsonDocument::fromJson(body);
      if (doc.isObject()) {
        const QString message = doc.object().value("message").toString();
        emit statusChanged("历史加载失败", false);
        emit errorMessage(message.isEmpty() ? "响应格式错误" : message);
        return;
      }
      if (!doc.isArray()) {
        emit statusChanged("历史加载失败", false);
        emit errorMessage("响应格式错误");
        return;
      }
      QVector<Candle> candles;
      for (const QJsonValue &value : doc.array()) {
        const Candle candle = parseCandle(value.toObject());
        if (isValidCandle(candle)) candles.push_back(candle);
      }
      if (candles.isEmpty()) return;
      updateKnownRange(candles);
      emit olderCandlesLoaded(candles);
      fetchOverlayEvents(knownStartMs_, knownEndMs_ + intervalMs(interval_) * 20);
    });
  }

  void loadReplayAround(qint64 cursorMs) {
    if (loadingReplay_ || backendBase_.isEmpty() || symbol_.isEmpty() || interval_.isEmpty() || cursorMs <= 0) return;
    loadingReplay_ = true;
    const qint64 step = intervalMs(interval_);
    const qint64 start = std::max<qint64>(0, cursorMs - step * 260);
    const qint64 end = cursorMs + step * 80;
    QUrl url(backendBase_ + "/api/candles");
    QUrlQuery query;
    query.addQueryItem("symbol", symbol_);
    query.addQueryItem("interval", interval_);
    query.addQueryItem("startTime", QString::number(start));
    query.addQueryItem("endTime", QString::number(end));
    url.setQuery(query);
    emit statusChanged("Replay加载中", false);
    QNetworkReply *reply = network_.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, start, end] {
      const QByteArray body = reply->readAll();
      reply->deleteLater();
      loadingReplay_ = false;
      if (reply->error() != QNetworkReply::NoError) {
        emit statusChanged("Replay加载失败", false);
        emit errorMessage(replyErrorMessage(reply, body));
        return;
      }
      const QJsonDocument doc = QJsonDocument::fromJson(body);
      if (doc.isObject()) {
        const QString message = doc.object().value("message").toString();
        emit statusChanged("Replay加载失败", false);
        emit errorMessage(message.isEmpty() ? "响应格式错误" : message);
        return;
      }
      if (!doc.isArray()) {
        emit statusChanged("Replay加载失败", false);
        emit errorMessage("响应格式错误");
        return;
      }
      QVector<Candle> candles;
      for (const QJsonValue &value : doc.array()) {
        const Candle candle = parseCandle(value.toObject());
        if (isValidCandle(candle)) candles.push_back(candle);
      }
      if (candles.isEmpty()) {
        emit errorMessage("Replay 时间附近没有K线数据");
        return;
      }
      updateKnownRange(candles);
      emit replayCandlesLoaded(candles);
      fetchOverlayEvents(start, end + intervalMs(interval_) * 20);
    });
  }

  void loadOverlayRange(qint64 startMs, qint64 endMs) {
    if (backendBase_.isEmpty() || symbol_.isEmpty()) return;
    if (startMs >= overlayLoadedStartMs_ && endMs <= overlayLoadedEndMs_) return;
    fetchOverlayEvents(std::min(startMs, overlayLoadedStartMs_ == 0 ? startMs : overlayLoadedStartMs_),
                       std::max(endMs, overlayLoadedEndMs_));
  }

  void loadOverlayStrategies() {
    if (backendBase_.isEmpty()) {
      emit overlayStrategiesLoaded({"n_in_range_variant"});
      return;
    }
    QUrl url(backendBase_ + "/api/strategy-overlay-strategies");
    QNetworkReply *reply = network_.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
      const QByteArray body = reply->readAll();
      reply->deleteLater();
      if (reply->error() != QNetworkReply::NoError) {
        emit errorMessage(replyErrorMessage(reply, body));
        emit overlayStrategiesLoaded({"n_in_range_variant"});
        return;
      }
      const QJsonDocument doc = QJsonDocument::fromJson(body);
      const QStringList strategies = parseStrategyList(doc);
      if (strategies.isEmpty()) {
        emit errorMessage("策略列表响应格式错误");
        emit overlayStrategiesLoaded({"n_in_range_variant"});
        return;
      }
      emit overlayStrategiesLoaded(strategies);
    });
  }

signals:
  void candlesLoaded(const QVector<Candle> &candles);
  void olderCandlesLoaded(const QVector<Candle> &candles);
  void replayCandlesLoaded(const QVector<Candle> &candles);
  void overlayEventsLoaded(const QJsonValue &events);
  void overlayStrategiesLoaded(const QStringList &strategies);
  void candleUpdated(const Candle &candle);
  void statusChanged(const QString &status, bool live);
  void errorMessage(const QString &message);
  void loadFailed(const QString &message);
  void debugLog(const QString &message);

private:
  static Candle parseCandle(const QJsonObject &obj) {
    return Candle{
      static_cast<qint64>(obj.value("timestamp").toDouble()),
      obj.value("open").toDouble(),
      obj.value("high").toDouble(),
      obj.value("low").toDouble(),
      obj.value("close").toDouble()
    };
  }

  static bool isValidCandle(const Candle &candle) {
    return candle.ms > 0
      && std::isfinite(candle.open)
      && std::isfinite(candle.high)
      && std::isfinite(candle.low)
      && std::isfinite(candle.close);
  }

  static QString normalizedStrategyName(const QString &strategyName) {
    const QString trimmed = strategyName.trimmed();
    return trimmed.isEmpty() ? QString("n_in_range_variant") : trimmed;
  }

  static QStringList parseStrategyList(const QJsonDocument &doc) {
    QJsonArray array;
    if (doc.isArray()) {
      array = doc.array();
    } else if (doc.isObject()) {
      const QJsonObject obj = doc.object();
      if (obj.value("strategies").isArray()) array = obj.value("strategies").toArray();
      else if (obj.value("data").isArray()) array = obj.value("data").toArray();
    }
    QStringList strategies;
    for (const QJsonValue &value : array) {
      QString name;
      if (value.isString()) {
        name = value.toString().trimmed();
      } else if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        name = obj.value("name").toString().trimmed();
        if (name.isEmpty()) name = obj.value("id").toString().trimmed();
        if (name.isEmpty()) name = obj.value("strategy").toString().trimmed();
      }
      if (!name.isEmpty() && !strategies.contains(name, Qt::CaseInsensitive)) strategies.push_back(name);
    }
    if (strategies.isEmpty()) strategies.push_back("n_in_range_variant");
    strategies.sort(Qt::CaseInsensitive);
    return strategies;
  }

  void connectSocket() {
    if (!realtimeEnabled_) {
      socket_.close();
      liveWatchdog_.stop();
      emit statusChanged("实时关闭", false);
      return;
    }
    socket_.close();
    liveWatchdog_.stop();
    lastRealtimeMessageMs_ = 0;
    liveWatchdog_.setInterval(static_cast<int>(std::clamp<qint64>(realtimeTimeoutMs() / 3, 10000, 60000)));
    emit statusChanged("连接实时", false);
    QUrl url(wsBase_ + "/ws/candles");
    QUrlQuery query;
    query.addQueryItem("symbol", symbol_);
    query.addQueryItem("interval", interval_);
    url.setQuery(query);
    emitDebugLog(QString("WS opening: %1").arg(url.toString()));
    socket_.open(url);
  }

  void fetchOverlayEvents(qint64 start, qint64 end) {
    if (symbol_.isEmpty() || higherInterval_.isEmpty() || lowerInterval_.isEmpty()) {
      emit overlayEventsLoaded(QJsonArray{});
      return;
    }
    QUrl url(backendBase_ + "/api/strategy-overlay-events");
    QUrlQuery query;
    query.addQueryItem("strategy", normalizedStrategyName(strategyName_));
    query.addQueryItem("symbol", symbol_);
    query.addQueryItem("higherInterval", higherInterval_);
    query.addQueryItem("lowerInterval", lowerInterval_);
    query.addQueryItem("startTime", QString::number(start));
    query.addQueryItem("endTime", QString::number(end));
    url.setQuery(query);
    QNetworkReply *reply = network_.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, start, end] {
      const QByteArray body = reply->readAll();
      reply->deleteLater();
      if (reply->error() != QNetworkReply::NoError) {
        emit statusChanged("策略事件加载失败", false);
        emit errorMessage(replyErrorMessage(reply, body));
        emit overlayEventsLoaded(QJsonArray{});
        return;
      }
      const QJsonDocument doc = QJsonDocument::fromJson(body);
      if (doc.isObject()) {
        const QJsonObject object = doc.object();
        if (object.value("layers").isArray()) {
          overlayLoadedStartMs_ = overlayLoadedStartMs_ == 0 ? start : std::min(overlayLoadedStartMs_, start);
          overlayLoadedEndMs_ = std::max(overlayLoadedEndMs_, end);
          mergeOverlayLayers(object);
          emit overlayEventsLoaded(overlayLayerDocument_);
        } else {
          const QString message = object.value("message").toString();
          emit statusChanged("策略事件加载失败", false);
          emit errorMessage(message.isEmpty() ? "响应格式错误" : message);
          emit overlayEventsLoaded(QJsonArray{});
        }
        return;
      }
      if (!doc.isArray()) {
        emit statusChanged("策略事件加载失败", false);
        emit errorMessage("响应格式错误");
        emit overlayEventsLoaded(QJsonArray{});
        return;
      }
      overlayLoadedStartMs_ = overlayLoadedStartMs_ == 0 ? start : std::min(overlayLoadedStartMs_, start);
      overlayLoadedEndMs_ = std::max(overlayLoadedEndMs_, end);
      mergeOverlayEvents(doc.isArray() ? doc.array() : QJsonArray{});
      emit overlayEventsLoaded(overlayEvents_);
    });
  }

  QString replyErrorMessage(QNetworkReply *reply, const QByteArray &body) const {
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
      const QString message = doc.object().value("message").toString().trimmed();
      if (!message.isEmpty()) return message;
    }
    const QString text = QString::fromUtf8(body).trimmed();
    if (!text.isEmpty() && text.size() < 240) return text;
    return reply->errorString();
  }

  qint64 realtimeTimeoutMs() const {
    const qint64 candleMs = intervalMs(interval_.toLower());
    return std::clamp<qint64>(candleMs * 5 / 2, 30000, 10 * 60000);
  }

  void updateKnownRange(const QVector<Candle> &candles) {
    if (candles.isEmpty()) return;
    qint64 start = candles.first().ms;
    qint64 end = candles.first().ms;
    for (const Candle &candle : candles) {
      start = std::min(start, candle.ms);
      end = std::max(end, candle.ms);
    }
    knownStartMs_ = knownStartMs_ == 0 ? start : std::min(knownStartMs_, start);
    knownEndMs_ = std::max(knownEndMs_, end);
  }

  void mergeOverlayEvents(const QJsonArray &events) {
    QHash<QString, QJsonObject> byId;
    for (const QJsonValue &value : overlayEvents_) {
      const QJsonObject event = value.toObject();
      byId.insert(event.value("id").toVariant().toString(), event);
    }
    for (const QJsonValue &value : events) {
      const QJsonObject event = value.toObject();
      byId.insert(event.value("id").toVariant().toString(), event);
    }
    overlayEvents_ = {};
    for (const QJsonObject &event : byId) overlayEvents_.append(event);
  }

  void mergeOverlayLayers(const QJsonObject &document) {
    if (overlayLayerDocument_.isEmpty()) overlayLayerDocument_ = document;
    else {
      for (auto it = document.begin(); it != document.end(); ++it) {
        if (it.key() != "layers") overlayLayerDocument_.insert(it.key(), it.value());
      }
    }
    QHash<QString, QJsonObject> byId;
    for (const QJsonValue &value : overlayLayerDocument_.value("layers").toArray()) {
      const QJsonObject layer = value.toObject();
      const QString id = layer.value("id").toString(QString::fromUtf8(QJsonDocument(layer).toJson(QJsonDocument::Compact)));
      byId.insert(id, layer);
    }
    for (const QJsonValue &value : document.value("layers").toArray()) {
      const QJsonObject layer = value.toObject();
      const QString id = layer.value("id").toString(QString::fromUtf8(QJsonDocument(layer).toJson(QJsonDocument::Compact)));
      byId.insert(id, layer);
    }
    QJsonArray layers;
    for (const QJsonObject &layer : byId) layers.append(layer);
    overlayLayerDocument_.insert("layers", layers);
  }

  void onSocketMessage(const QString &message) {
    emitDebugLog(QString("WS raw: %1").arg(message.left(1000)));
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
      emitDebugLog("WS ignored: JSON root is not an object");
      return;
    }
    QJsonObject obj = doc.object();
    if (obj.contains("data") && obj.value("data").isObject()) obj = obj.value("data").toObject();
    if (obj.contains("candle") && obj.value("candle").isObject()) obj = obj.value("candle").toObject();
    const Candle candle = parseCandle(obj);
    if (!isValidCandle(candle)) {
      emitDebugLog(QString("WS invalid candle: timestamp=%1 open=%2 high=%3 low=%4 close=%5")
        .arg(candle.ms)
        .arg(candle.open)
        .arg(candle.high)
        .arg(candle.low)
        .arg(candle.close));
      emit statusChanged("实时格式错误", false);
      return;
    }
    lastRealtimeMessageMs_ = QDateTime::currentMSecsSinceEpoch();
    emitDebugLog(QString("WS parsed candle: %1 O=%2 H=%3 L=%4 C=%5")
      .arg(QDateTime::fromMSecsSinceEpoch(candle.ms).toString("yyyy-MM-dd HH:mm:ss.zzz"))
      .arg(candle.open)
      .arg(candle.high)
      .arg(candle.low)
      .arg(candle.close));
    emit statusChanged("实时", true);
    emitDebugLog("WS emit candleUpdated");
    emit candleUpdated(candle);
  }

  void emitDebugLog(const QString &message) {
    emit debugLog(QDateTime::currentDateTime().toString("HH:mm:ss.zzz  ") + message);
  }

  QString backendBase_;
  QString wsBase_;
  QString symbol_;
  QString interval_;
  QString strategyName_ = "n_in_range_variant";
  QString higherInterval_;
  QString lowerInterval_;
  QNetworkAccessManager network_;
  QWebSocket socket_;
  QTimer liveWatchdog_;
  bool loadingOlder_ = false;
  bool loadingReplay_ = false;
  bool realtimeEnabled_ = true;
  qint64 lastRealtimeMessageMs_ = 0;
  qint64 knownStartMs_ = 0;
  qint64 knownEndMs_ = 0;
  qint64 overlayLoadedStartMs_ = 0;
  qint64 overlayLoadedEndMs_ = 0;
  QJsonArray overlayEvents_;
  QJsonObject overlayLayerDocument_;
};

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow() {
    setWindowTitle("Q4J Market Structure Desk");
    setWindowIcon(QIcon(":/app.ico"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setMouseTracking(true);
    resize(1440, 860);
    buildUi();
    bindSignals();
    applyTheme();
    loadBackendSettings();
    loadStrategySettings();
    if (client_.hasConfiguredBackend()) client_.loadOverlayStrategies();
    loadIndicatorSettings();
    loadDisplaySettings();
    if (client_.hasConfiguredBackend()) {
      refresh();
    } else if (showBackendDialog(true)) {
      saveBackendSettings();
      refresh();
    } else {
      status_->setText("○ 后端未配置");
    }
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (watched == titleBar_) {
      if (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove) {
        if (!resizingWindow_) clearResizeCursors();
      }
      if (event->type() == QEvent::MouseButtonDblClick) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
          toggleMaximized();
          return true;
        }
      }
      if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
          windowDragging_ = true;
          windowDragStart_ = mouse->globalPosition().toPoint() - frameGeometry().topLeft();
          return true;
        }
      }
      if (event->type() == QEvent::MouseMove && windowDragging_) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (!maximizedAnimated_ && !isMaximized()) move(mouse->globalPosition().toPoint() - windowDragStart_);
        return true;
      }
      if (event->type() == QEvent::MouseButtonRelease) {
        windowDragging_ = false;
        return true;
      }
    }
    if (watched == centralWidget() || watched == chart_ || watched == header_ || watched == footer_) {
      if (auto *mouse = dynamic_cast<QMouseEvent *>(event)) {
        const QPoint global = mouse->globalPosition().toPoint();
        const QPoint local = mapFromGlobal(global);
        if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton) {
          if (startWindowResize(local, global)) return true;
        }
        if (event->type() == QEvent::MouseMove) {
          if (resizingWindow_) {
            resizeWindowTo(global);
            return true;
          }
          updateResizeCursor(local);
        }
        if (event->type() == QEvent::MouseButtonRelease && resizingWindow_) {
          finishWindowResize();
          return true;
        }
      }
    }
    return QMainWindow::eventFilter(watched, event);
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      if (startWindowResize(event->position().toPoint(), event->globalPosition().toPoint())) {
        event->accept();
        return;
      }
    }
    QMainWindow::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    if (resizingWindow_) {
      resizeWindowTo(event->globalPosition().toPoint());
      event->accept();
      return;
    }
    updateResizeCursor(event->position().toPoint());
    QMainWindow::mouseMoveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton && resizingWindow_) {
      finishWindowResize();
      event->accept();
      return;
    }
    QMainWindow::mouseReleaseEvent(event);
  }

  void leaveEvent(QEvent *event) override {
    if (!resizingWindow_) clearResizeCursors();
    QMainWindow::leaveEvent(event);
  }

private:
  Qt::Edges resizeEdgesAt(const QPoint &pos) const {
    if (maximizedAnimated_ || isMaximized()) return {};
    constexpr int margin = 7;
    Qt::Edges edges;
    if (pos.x() <= margin) edges |= Qt::LeftEdge;
    if (pos.x() >= width() - margin) edges |= Qt::RightEdge;
    if (pos.y() <= margin) edges |= Qt::TopEdge;
    if (pos.y() >= height() - margin) edges |= Qt::BottomEdge;
    return edges;
  }

  QCursor cursorForEdges(Qt::Edges edges) const {
    if ((edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::TopEdge)) ||
        (edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::BottomEdge))) {
      return Qt::SizeFDiagCursor;
    }
    if ((edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::TopEdge)) ||
        (edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::BottomEdge))) {
      return Qt::SizeBDiagCursor;
    }
    if (edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge)) return Qt::SizeHorCursor;
    if (edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge)) return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
  }

  void clearResizeCursors() {
    unsetCursor();
    if (centralWidget()) centralWidget()->unsetCursor();
    if (chart_) chart_->unsetCursor();
    if (header_) header_->unsetCursor();
    if (footer_) footer_->unsetCursor();
    if (titleBar_) titleBar_->unsetCursor();
  }

  void applyResizeCursor(const QCursor &cursor) {
    setCursor(cursor);
    if (centralWidget()) centralWidget()->setCursor(cursor);
    if (chart_) chart_->setCursor(cursor);
    if (header_) header_->setCursor(cursor);
    if (footer_) footer_->setCursor(cursor);
    if (titleBar_) titleBar_->unsetCursor();
  }

  void updateResizeCursor(const QPoint &pos) {
    const Qt::Edges edges = resizeEdgesAt(pos);
    if (edges != Qt::Edges{}) {
      applyResizeCursor(cursorForEdges(edges));
    } else {
      clearResizeCursors();
    }
  }

  bool startWindowResize(const QPoint &localPos, const QPoint &globalPos) {
    const Qt::Edges edges = resizeEdgesAt(localPos);
    if (edges == Qt::Edges{}) return false;
    resizingWindow_ = true;
    resizeEdges_ = edges;
    resizeStartPos_ = globalPos;
    resizeStartGeometry_ = geometry();
    return true;
  }

  void finishWindowResize() {
    resizingWindow_ = false;
    resizeEdges_ = {};
    clearResizeCursors();
  }

  void resizeWindowTo(const QPoint &globalPos) {
    QRect next = resizeStartGeometry_;
    const QPoint delta = globalPos - resizeStartPos_;
    const QSize min = minimumSize().expandedTo(QSize(980, 620));
    if (resizeEdges_.testFlag(Qt::LeftEdge)) {
      const int newLeft = std::min(next.right() - min.width(), next.left() + delta.x());
      next.setLeft(newLeft);
    }
    if (resizeEdges_.testFlag(Qt::RightEdge)) {
      next.setRight(std::max(next.left() + min.width(), next.right() + delta.x()));
    }
    if (resizeEdges_.testFlag(Qt::TopEdge)) {
      const int newTop = std::min(next.bottom() - min.height(), next.top() + delta.y());
      next.setTop(newTop);
    }
    if (resizeEdges_.testFlag(Qt::BottomEdge)) {
      next.setBottom(std::max(next.top() + min.height(), next.bottom() + delta.y()));
    }
    setGeometry(next);
  }

  QIcon annotationIcon(ChartWidget::AnnotationTool tool) const {
    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor fg = dark_ ? QColor("#f4efe3") : QColor("#131916");
    const QColor accent = QColor("#f0b64f");
    p.setPen(QPen(fg, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (tool == ChartWidget::AnnotationTool::None) {
      QPolygonF arrow{QPointF(8, 5), QPointF(20, 16), QPointF(14, 17), QPointF(17, 24), QPointF(13, 25), QPointF(10, 18), QPointF(6, 22)};
      p.drawPolygon(arrow);
    } else if (tool == ChartWidget::AnnotationTool::LongBlock || tool == ChartWidget::AnnotationTool::ShortBlock) {
      const bool isLong = tool == ChartWidget::AnnotationTool::LongBlock;
      QColor profit(32, 201, 151, 135);
      QColor loss(239, 95, 120, 135);
      QRectF top(7, 5, 14, 9);
      QRectF bottom(7, 14, 14, 9);
      p.fillRect(isLong ? top : bottom, profit);
      p.fillRect(isLong ? bottom : top, loss);
      p.setPen(QPen(fg, 1.6));
      p.drawRect(QRectF(7, 5, 14, 18));
      p.setPen(QPen(accent, 1.4));
      p.drawLine(QPointF(5, 14), QPointF(23, 14));
    } else if (tool == ChartWidget::AnnotationTool::SegmentLine) {
      p.drawLine(QPointF(7, 20), QPointF(21, 8));
    } else if (tool == ChartWidget::AnnotationTool::HorizontalLine) {
      p.drawLine(QPointF(5, 14), QPointF(23, 14));
    } else if (tool == ChartWidget::AnnotationTool::VerticalLine) {
      p.drawLine(QPointF(14, 5), QPointF(14, 23));
    } else if (tool == ChartWidget::AnnotationTool::Polyline) {
      QPolygonF line{QPointF(5, 20), QPointF(11, 9), QPointF(17, 15), QPointF(23, 6)};
      p.drawPolyline(line);
    } else if (tool == ChartWidget::AnnotationTool::Rectangle) {
      p.drawRect(QRectF(7, 7, 14, 14));
    }
    return QIcon(pixmap);
  }

  QIcon magnetIcon() const {
    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor fg = dark_ ? QColor("#f4efe3") : QColor("#131916");
    p.setPen(QPen(fg, 2, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(6, 6, 6, 16), 90 * 16, 180 * 16);
    p.drawArc(QRectF(16, 6, 6, 16), -90 * 16, 180 * 16);
    p.drawLine(QPointF(9, 6), QPointF(9, 11));
    p.drawLine(QPointF(19, 6), QPointF(19, 11));
    p.drawLine(QPointF(9, 22), QPointF(9, 18));
    p.drawLine(QPointF(19, 22), QPointF(19, 18));
    return QIcon(pixmap);
  }

  QPushButton *annotationButton(const QString &tip, ChartWidget::AnnotationTool tool) {
    auto *button = new QPushButton;
    button->setObjectName("annotationToolButton");
    button->setCheckable(true);
    button->setFixedSize(34, 34);
    button->setIcon(annotationIcon(tool));
    button->setIconSize(QSize(24, 24));
    button->setToolTip(tip);
    annotationGroup_->addButton(button, static_cast<int>(tool));
    return button;
  }

  QFrame *buildAnnotationToolbar() {
    auto *toolbar = new QFrame;
    toolbar->setObjectName("annotationToolbar");
    toolbar->setFixedWidth(44);
    auto *layout = new QVBoxLayout(toolbar);
    layout->setContentsMargins(5, 6, 5, 6);
    layout->setSpacing(5);
    annotationGroup_ = new QButtonGroup(this);
    annotationGroup_->setExclusive(true);
    auto *cursor = annotationButton("选择/拖动图表", ChartWidget::AnnotationTool::None);
    cursor->setChecked(true);
    layout->addWidget(cursor);
    layout->addWidget(annotationButton("开仓区块 Long", ChartWidget::AnnotationTool::LongBlock));
    layout->addWidget(annotationButton("开仓区块 Short", ChartWidget::AnnotationTool::ShortBlock));
    layout->addWidget(annotationButton("单向横线：拖拽确定长度", ChartWidget::AnnotationTool::SegmentLine));
    layout->addWidget(annotationButton("水平线：全屏", ChartWidget::AnnotationTool::HorizontalLine));
    layout->addWidget(annotationButton("垂直线：全屏", ChartWidget::AnnotationTool::VerticalLine));
    layout->addWidget(annotationButton("折线：连续点击，右键或双击结束", ChartWidget::AnnotationTool::Polyline));
    layout->addWidget(annotationButton("矩形方框", ChartWidget::AnnotationTool::Rectangle));
    auto *divider = new QFrame;
    divider->setObjectName("annotationDivider");
    divider->setFixedSize(24, 1);
    layout->addWidget(divider);
    magnetButton_ = new QPushButton;
    magnetButton_->setObjectName("annotationToolButton");
    magnetButton_->setCheckable(true);
    magnetButton_->setChecked(true);
    magnetButton_->setFixedSize(34, 34);
    magnetButton_->setIcon(magnetIcon());
    magnetButton_->setIconSize(QSize(24, 24));
    magnetButton_->setToolTip("磁铁吸附：吸附到最近 K 线 OHLC");
    layout->addWidget(magnetButton_);
    layout->addStretch(1);
    return toolbar;
  }

  void buildUi() {
    auto *root = new QWidget;
    root->setObjectName("appShell");
    root->setMouseTracking(true);
    root->installEventFilter(this);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    setCentralWidget(root);

    titleBar_ = new QFrame;
    titleBar_->setObjectName("titleBar");
    titleBar_->installEventFilter(this);
    auto *titleLayout = new QHBoxLayout(titleBar_);
    titleLayout->setContentsMargins(10, 0, 6, 0);
    titleLayout->setSpacing(8);
    auto *titleBadge = new QLabel("Q4J");
    titleBadge->setObjectName("titleBadge");
    auto *titleText = new QLabel("Execution Map");
    titleText->setObjectName("titleText");
#ifdef Q_OS_MACOS
    minimize_ = new QPushButton;
    maximize_ = new QPushButton;
    close_ = new QPushButton;
    close_->setObjectName("macCloseButton");
    minimize_->setObjectName("macMinimizeButton");
    maximize_->setObjectName("macMaximizeButton");
    close_->setFixedSize(13, 13);
    minimize_->setFixedSize(13, 13);
    maximize_->setFixedSize(13, 13);
    titleLayout->addWidget(close_);
    titleLayout->addWidget(minimize_);
    titleLayout->addWidget(maximize_);
    titleLayout->addSpacing(8);
    titleLayout->addWidget(titleBadge);
    titleLayout->addWidget(titleText);
    titleLayout->addStretch(1);
#else
    minimize_ = new QPushButton("−");
    maximize_ = new QPushButton("▢");
    close_ = new QPushButton("×");
    minimize_->setObjectName("windowButton");
    maximize_->setObjectName("windowButton");
    close_->setObjectName("closeButton");
    minimize_->setFixedSize(30, 24);
    maximize_->setFixedSize(30, 24);
    close_->setFixedSize(34, 24);
    titleLayout->addWidget(titleBadge);
    titleLayout->addWidget(titleText);
    titleLayout->addStretch(1);
    titleLayout->addWidget(minimize_);
    titleLayout->addWidget(maximize_);
    titleLayout->addWidget(close_);
#endif
    layout->addWidget(titleBar_);

    auto *header = new QFrame;
    header->setObjectName("header");
    header_ = header;
    header_->setMouseTracking(true);
    header_->installEventFilter(this);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(9, 7, 9, 7);
    headerLayout->setSpacing(8);

    symbol_ = new QLineEdit("XAUUSD");
    symbol_->setObjectName("symbolInput");
    symbol_->setPlaceholderText("Symbol");
    interval_ = new QComboBox;
    interval_->setObjectName("intervalInput");
    interval_->addItems({"1M", "2M", "3M", "5M", "10M", "15M", "30M", "1H", "4H", "1D"});
    settings_ = new QPushButton("策略设置");
    settings_->setObjectName("toolButton");
    indicators_ = new QPushButton("指标");
    indicators_->setObjectName("toolButton");
    replayToggle_ = new QPushButton("Replay");
    replayToggle_->setObjectName("toolButton");
    replayToggle_->setCheckable(true);
    replayTime_ = new QDateTimeEdit;
    replayTime_->setObjectName("replayTime");
    replayTime_->setDisplayFormat("yyyy-MM-dd HH:mm");
    replayTime_->setCalendarPopup(true);
    replayTime_->setTimeSpec(Qt::LocalTime);
    replayTime_->setMinimumDateTime(QDateTime(QDate(2000, 1, 1), QTime(0, 0)));
    replayTime_->setMaximumDateTime(QDateTime(QDate(2099, 12, 31), QTime(23, 59)));
    replayTime_->setDateTime(normalizeReplayMinute(QDateTime::currentDateTime()));
    replayTime_->setToolTip("Replay 时间，精度到分钟");
    replayPlay_ = new QPushButton("▶");
    replayPlay_->setObjectName("iconButton");
    replayPlay_->setCheckable(true);
    replayPlay_->setToolTip("播放 / 暂停 Replay");
    replayStep_ = new QPushButton("›");
    replayStep_->setObjectName("iconButton");
    replayStep_->setToolTip("Replay 前进一根K线");
    backend_ = new QPushButton("服务端设置");
    backend_->setObjectName("toolButton");
    wsLogButton_ = new QPushButton("WS日志");
    wsLogButton_->setObjectName("toolButton");
    updateButton_ = new QPushButton("检查更新");
    updateButton_->setObjectName("toolButton");
    theme_ = new QPushButton("☾");
    theme_->setObjectName("iconButton");
    refresh_ = new QPushButton("刷新");
    refresh_->setObjectName("refreshButton");
    status_ = new QLabel("连接中");
    status_->setObjectName("status");
    symbol_->setFixedWidth(132);
    interval_->setFixedWidth(73);
    settings_->setFixedWidth(75);
    indicators_->setFixedWidth(52);
    replayToggle_->setFixedWidth(58);
    replayTime_->setFixedWidth(142);
    replayPlay_->setFixedWidth(34);
    replayStep_->setFixedWidth(34);
    backend_->setFixedWidth(88);
    wsLogButton_->setFixedWidth(68);
    updateButton_->setFixedWidth(76);
    theme_->setFixedWidth(34);
    refresh_->setFixedWidth(56);
    status_->setFixedWidth(120);
    headerLayout->addWidget(symbol_);
    headerLayout->addWidget(interval_);
    headerLayout->addWidget(settings_);
    headerLayout->addWidget(indicators_);
    headerLayout->addWidget(replayToggle_);
    headerLayout->addWidget(replayTime_);
    headerLayout->addWidget(replayPlay_);
    headerLayout->addWidget(replayStep_);
    headerLayout->addWidget(backend_);
    headerLayout->addWidget(wsLogButton_);
    headerLayout->addWidget(updateButton_);
    headerLayout->addStretch(1);
    headerLayout->addWidget(theme_);
    headerLayout->addWidget(refresh_);
    headerLayout->addWidget(status_);
    layout->addWidget(header);

    auto *chartRow = new QWidget;
    chartRow->setObjectName("chartRow");
    auto *chartRowLayout = new QHBoxLayout(chartRow);
    chartRowLayout->setContentsMargins(0, 0, 0, 0);
    chartRowLayout->setSpacing(8);
    annotationToolbar_ = buildAnnotationToolbar();
    chartRowLayout->addWidget(annotationToolbar_);
    chart_ = new ChartWidget;
    chart_->installEventFilter(this);
    chartRowLayout->addWidget(chart_, 1);
    layout->addWidget(chartRow, 1);

    auto *footer = new QFrame;
    footer->setObjectName("footer");
    footer_ = footer;
    footer_->setMouseTracking(true);
    footer_->installEventFilter(this);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(8, 6, 8, 6);
    footerLayout->setSpacing(16);
    ohlc_ = new QLabel("O -- H -- L -- C --");
    range_ = new QLabel("Visible Range --");
    events_ = new QLabel("Events --");
    timeZone_ = new QComboBox;
    timeZone_->setObjectName("footerTimeZone");
    timeZone_->setFixedWidth(180);
    timeZone_->setToolTip("X轴时间时区");
    populateTimeZones();
    footerLayout->addWidget(ohlc_, 2);
    footerLayout->addWidget(range_, 3);
    footerLayout->addWidget(events_, 1);
    footerLayout->addWidget(timeZone_, 0, Qt::AlignRight);
    layout->addWidget(footer);

    buildSettingsDialog();
    buildIndicatorDialog();
    buildBackendDialog();
    buildDebugDialog();
  }

  void buildDebugDialog() {
    debugDialog_ = new QDialog(this);
    debugDialog_->setWindowTitle("WS 调试日志");
    debugDialog_->setMinimumSize(760, 460);
    auto *layout = new QVBoxLayout(debugDialog_);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);
    debugLog_ = new QPlainTextEdit;
    debugLog_->setObjectName("debugLog");
    debugLog_->setReadOnly(true);
    debugLog_->setMaximumBlockCount(1200);
    layout->addWidget(debugLog_, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *clear = buttons->addButton("清空", QDialogButtonBox::ResetRole);
    layout->addWidget(buttons);
    connect(clear, &QPushButton::clicked, debugLog_, &QPlainTextEdit::clear);
    connect(buttons, &QDialogButtonBox::rejected, debugDialog_, &QDialog::hide);
  }

  void buildSettingsDialog() {
    settingsDialog_ = new QDialog(this);
    settingsDialog_->setWindowTitle("策略设置");
    settingsDialog_->setMinimumSize(420, 330);
    auto *layout = new QFormLayout(settingsDialog_);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(14);
    higher_ = new QComboBox;
    lower_ = new QComboBox;
    strategy_ = new QComboBox;
    strategy_->setEditable(true);
    strategy_->setInsertPolicy(QComboBox::NoInsert);
    strategy_->setMinimumHeight(34);
    strategy_->lineEdit()->setPlaceholderText("输入或选择策略名");
    strategy_->addItem("n_in_range_variant");
    auto *strategyCompleter = new QCompleter(strategy_->model(), strategy_);
    strategyCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    strategyCompleter->setFilterMode(Qt::MatchContains);
    strategyCompleter->setCompletionMode(QCompleter::PopupCompletion);
    strategy_->setCompleter(strategyCompleter);
    higher_->setMinimumHeight(34);
    lower_->setMinimumHeight(34);
    higher_->addItems({"5m", "10m", "15m", "30m", "1h", "4h"});
    lower_->addItems({"1m", "2m", "3m", "5m", "10m", "15m"});
    fvgCircleEnabled_ = new QCheckBox("显示 FVG Circle");
    fvgCircleEnabled_->setChecked(chart_->fvgCircleSettings().enabled);
    fvgCircleN_ = new QSpinBox;
    fvgCircleN_->setRange(1, 50);
    fvgCircleN_->setValue(chart_->fvgCircleSettings().leftRightBars);
    fvgCircleN_->setMinimumHeight(34);
    fvgCircleMinGapTicks_ = new QSpinBox;
    fvgCircleMinGapTicks_->setRange(0, 10000);
    fvgCircleMinGapTicks_->setValue(chart_->fvgCircleSettings().minGapTicks);
    fvgCircleMinGapTicks_->setMinimumHeight(34);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    layout->addRow("策略", strategy_);
    layout->addRow("高周期", higher_);
    layout->addRow("低周期", lower_);
    auto *indicatorTitle = new QLabel("内置指标");
    indicatorTitle->setObjectName("sectionLabel");
    layout->addRow(indicatorTitle);
    layout->addRow("FVG Circle", fvgCircleEnabled_);
    layout->addRow("Left / Right N", fvgCircleN_);
    layout->addRow("Min gap ticks", fvgCircleMinGapTicks_);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, settingsDialog_, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
      chart_->setFvgCircleSettings(fvgCircleEnabled_->isChecked(), fvgCircleN_->value(), fvgCircleMinGapTicks_->value());
      saveStrategySettings();
      saveIndicatorSettings();
      settingsDialog_->accept();
      refresh();
    });
  }

  void buildIndicatorDialog() {
    indicatorDialog_ = new QDialog(this);
    indicatorDialog_->setWindowTitle("自定义指标");
    indicatorDialog_->setMinimumSize(820, 560);
    auto *layout = new QVBoxLayout(indicatorDialog_);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(12);

    auto *topBar = new QWidget;
    auto *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(8);
    auto *customTitle = new QLabel("自定义指标");
    customTitle->setObjectName("dialogTitle");
    auto *newIndicator = new QPushButton("新建");
    auto *copyBuiltinFvg = new QPushButton("复制内置 FVG");
    auto *openIndicators = new QPushButton("打开目录");
    auto *reloadIndicators = new QPushButton("重载");
    newIndicator->setObjectName("toolButton");
    copyBuiltinFvg->setObjectName("toolButton");
    openIndicators->setObjectName("toolButton");
    reloadIndicators->setObjectName("toolButton");
    topLayout->addWidget(customTitle);
    topLayout->addStretch(1);
    topLayout->addWidget(newIndicator);
    topLayout->addWidget(copyBuiltinFvg);
    topLayout->addWidget(openIndicators);
    topLayout->addWidget(reloadIndicators);
    layout->addWidget(topBar);

    auto *body = new QSplitter(Qt::Horizontal);
    customIndicatorList_ = new QWidget;
    customIndicatorList_->setObjectName("customIndicatorList");
    customIndicatorLayout_ = new QVBoxLayout(customIndicatorList_);
    customIndicatorLayout_->setContentsMargins(8, 8, 8, 8);
    customIndicatorLayout_->setSpacing(6);
    auto *customIndicatorScroll = new QScrollArea;
    customIndicatorScroll->setWidgetResizable(true);
    customIndicatorScroll->setFrameShape(QFrame::NoFrame);
    customIndicatorScroll->setWidget(customIndicatorList_);
    body->addWidget(customIndicatorScroll);

    auto *sidePanel = new QWidget;
    auto *sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(12, 10, 12, 10);
    sideLayout->setSpacing(10);
    indicatorErrorText_ = new QPlainTextEdit;
    indicatorErrorText_->setObjectName("debugLog");
    indicatorErrorText_->setReadOnly(true);
    indicatorErrorText_->setMinimumHeight(160);
    auto *errorLabel = new QLabel("脚本错误");
    errorLabel->setObjectName("sectionLabel");
    auto *hint = new QLabel("指标参数显示在左侧列表中。图表左上角 Layers 也可以直接开关每个指标。");
    hint->setWordWrap(true);
    hint->setObjectName("mutedLabel");
    sideLayout->addWidget(errorLabel);
    sideLayout->addWidget(indicatorErrorText_, 1);
    sideLayout->addWidget(hint);
    body->addWidget(sidePanel);
    body->setStretchFactor(0, 3);
    body->setStretchFactor(1, 2);
    layout->addWidget(body, 1);
    connect(newIndicator, &QPushButton::clicked, this, [this] {
      openIndicatorEditor({}, uniqueIndicatorPath("custom-indicator"), newIndicatorTemplate(), "新建指标");
    });
    connect(copyBuiltinFvg, &QPushButton::clicked, this, [this] {
      openIndicatorEditor({}, uniqueIndicatorPath("fvg-circle-copy"), builtinFvgCircleTemplate(), "复制内置 FVG");
    });
    connect(openIndicators, &QPushButton::clicked, this, [this] {
      QDesktopServices::openUrl(QUrl::fromLocalFile(chart_->indicatorScriptDirectory()));
    });
    connect(reloadIndicators, &QPushButton::clicked, this, [this] {
      chart_->reloadCustomIndicators();
      loadIndicatorSettings();
      rebuildCustomIndicatorList();
    });
    rebuildCustomIndicatorList();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, indicatorDialog_, &QDialog::hide);
  }

  void rebuildCustomIndicatorList() {
    if (!customIndicatorLayout_) return;
    while (QLayoutItem *item = customIndicatorLayout_->takeAt(0)) {
      if (QWidget *widget = item->widget()) widget->deleteLater();
      delete item;
    }
    const QVector<IndicatorScript> scripts = chart_->customIndicators();
    if (!chart_->indicatorScriptRuntimeAvailable()) {
      auto *disabled = new QLabel("当前构建缺少 QtQml/QJSEngine，JS 自定义指标不可用。Windows/macOS Actions 已配置安装 Qt Declarative 后会启用。");
      disabled->setWordWrap(true);
      customIndicatorLayout_->addWidget(disabled);
    }
    if (scripts.isEmpty()) {
      auto *empty = new QLabel(QString("将 .js 指标文件放到：%1").arg(chart_->indicatorScriptDirectory()));
      empty->setWordWrap(true);
      customIndicatorLayout_->addWidget(empty);
      return;
    }
    for (const IndicatorScript &script : scripts) {
      auto *row = new QWidget;
      row->setObjectName("indicatorRow");
      auto *rowLayout = new QHBoxLayout(row);
      rowLayout->setContentsMargins(10, 8, 10, 8);
      rowLayout->setSpacing(8);
      auto *check = new QCheckBox(QString("%1  (%2)").arg(script.name, QFileInfo(script.path).fileName()));
      check->setChecked(script.enabled);
      check->setEnabled(chart_->indicatorScriptRuntimeAvailable());
      check->setToolTip(script.path);
      auto *edit = new QPushButton("编辑");
      auto *copy = new QPushButton("复制");
      auto *remove = new QPushButton("删除");
      edit->setObjectName("toolButton");
      copy->setObjectName("toolButton");
      remove->setObjectName("toolButton");
      edit->setFixedWidth(52);
      copy->setFixedWidth(52);
      remove->setFixedWidth(52);
      rowLayout->addWidget(check, 1);
      rowLayout->addWidget(edit);
      rowLayout->addWidget(copy);
      rowLayout->addWidget(remove);
      customIndicatorLayout_->addWidget(row);
      connect(check, &QCheckBox::toggled, this, [this, id = script.id](bool enabled) {
        chart_->setCustomIndicatorEnabled(id, enabled);
        saveIndicatorSettings();
        updateIndicatorErrorText();
      });
      connect(edit, &QPushButton::clicked, this, [this, script] {
        openIndicatorEditor(script.path, script.path, readTextFile(script.path), QString("编辑 %1").arg(script.name));
      });
      connect(copy, &QPushButton::clicked, this, [this, script] {
        const QString base = QFileInfo(script.path).baseName() + "-copy";
        openIndicatorEditor({}, uniqueIndicatorPath(base), readTextFile(script.path), QString("复制 %1").arg(script.name));
      });
      connect(remove, &QPushButton::clicked, this, [this, script] {
        deleteIndicatorScript(script);
      });
      const QVector<IndicatorInput> inputs = chart_->customIndicatorInputs(script.id);
      if (!inputs.isEmpty()) {
        auto *formWidget = new QWidget;
        formWidget->setObjectName("indicatorParams");
        auto *form = new QFormLayout(formWidget);
        form->setContentsMargins(22, 0, 10, 10);
        form->setSpacing(7);
        for (const IndicatorInput &input : inputs) addIndicatorInputControl(form, script.id, input);
        customIndicatorLayout_->addWidget(formWidget);
      }
    }
    updateIndicatorErrorText();
  }

  void addIndicatorInputControl(QFormLayout *form, const QString &scriptId, const IndicatorInput &input) {
    if (input.type == "bool") {
      auto *control = new QCheckBox;
      control->setChecked(input.value.toBool());
      control->setEnabled(chart_->indicatorScriptRuntimeAvailable());
      form->addRow(input.label, control);
      connect(control, &QCheckBox::toggled, this, [this, scriptId, inputId = input.id](bool value) {
        chart_->setCustomIndicatorInputValue(scriptId, inputId, value);
        saveIndicatorSettings();
        updateIndicatorErrorText();
      });
      return;
    }
    if (input.type == "int") {
      auto *control = new QSpinBox;
      control->setRange(static_cast<int>(std::max(-1000000.0, input.min)), static_cast<int>(std::min(1000000.0, input.max)));
      control->setValue(input.value.toInt());
      control->setMinimumHeight(30);
      control->setEnabled(chart_->indicatorScriptRuntimeAvailable());
      form->addRow(input.label, control);
      connect(control, &QSpinBox::valueChanged, this, [this, scriptId, inputId = input.id](int value) {
        chart_->setCustomIndicatorInputValue(scriptId, inputId, value);
        saveIndicatorSettings();
        updateIndicatorErrorText();
      });
      return;
    }
    auto *control = new QDoubleSpinBox;
    control->setDecimals(6);
    control->setRange(std::max(-1000000.0, input.min), std::min(1000000.0, input.max));
    control->setValue(input.value.toDouble());
    control->setMinimumHeight(30);
    control->setEnabled(chart_->indicatorScriptRuntimeAvailable());
    form->addRow(input.label, control);
    connect(control, &QDoubleSpinBox::valueChanged, this, [this, scriptId, inputId = input.id](double value) {
      chart_->setCustomIndicatorInputValue(scriptId, inputId, value);
      saveIndicatorSettings();
      updateIndicatorErrorText();
    });
  }

  void updateIndicatorErrorText() {
    if (!indicatorErrorText_) return;
    const QStringList errors = chart_->indicatorErrors();
    indicatorErrorText_->setPlainText(errors.isEmpty() ? "无" : errors.join('\n'));
  }

  QString readTextFile(const QString &path) const {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
  }

  QString sanitizedIndicatorBaseName(QString value) const {
    value = value.trimmed().toLower();
    QString sanitized;
    bool previousDash = false;
    for (const QChar ch : value) {
      if (ch.isLetterOrNumber()) {
        sanitized.append(ch);
        previousDash = false;
      } else if (ch == '_' || ch == '-' || ch.isSpace()) {
        if (!previousDash && !sanitized.isEmpty()) {
          sanitized.append('-');
          previousDash = true;
        }
      }
    }
    while (sanitized.endsWith('-') || sanitized.endsWith('_')) sanitized.chop(1);
    return sanitized.isEmpty() ? "custom-indicator" : sanitized;
  }

  QString uniqueIndicatorPath(const QString &baseName) const {
    const QString dir = chart_->indicatorScriptDirectory();
    const QString base = sanitizedIndicatorBaseName(baseName);
    QString path = QDir(dir).filePath(base + ".js");
    int index = 2;
    while (QFileInfo::exists(path)) {
      path = QDir(dir).filePath(QString("%1-%2.js").arg(base).arg(index++));
    }
    return path;
  }

  QString indicatorNameFromCode(const QString &code) const {
    const QRegularExpression pattern("indicator\\s*\\(\\s*([\"'`])([^\"'`]+)\\1");
    const QRegularExpressionMatch match = pattern.match(code);
    return match.hasMatch() ? match.captured(2).trimmed() : "Custom Indicator";
  }

  QString indicatorPathFromCode(const QString &code) const {
    return QDir(chart_->indicatorScriptDirectory()).filePath(sanitizedIndicatorBaseName(indicatorNameFromCode(code)) + ".js");
  }

  QString resolvedIndicatorSavePath(const QString &code, const QString &originalPath) const {
    const QString target = indicatorPathFromCode(code);
    const QString originalAbs = QFileInfo(originalPath).absoluteFilePath();
    const QString targetAbs = QFileInfo(target).absoluteFilePath();
    if (QFileInfo::exists(target) && targetAbs != originalAbs) {
      return uniqueIndicatorPath(QFileInfo(target).baseName());
    }
    return target;
  }

  QString formatIndicatorCode(const QString &code) const {
    QString normalized = code;
    normalized.replace("\r\n", "\n").replace('\r', '\n');
    QStringList lines = normalized.split('\n');
    QStringList out;
    int indent = 0;
    bool previousBlank = false;

    auto startsWithClosing = [](const QString &line) {
      return line.startsWith('}') || line.startsWith(")") || line.startsWith("]");
    };

    auto countOutsideStrings = [](const QString &line, QChar target) {
      int count = 0;
      bool single = false;
      bool dbl = false;
      bool tpl = false;
      bool escaped = false;
      for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line[i];
        if (escaped) {
          escaped = false;
          continue;
        }
        if (ch == '\\') {
          escaped = true;
          continue;
        }
        if (!dbl && !tpl && ch == '\'') single = !single;
        else if (!single && !tpl && ch == '"') dbl = !dbl;
        else if (!single && !dbl && ch == '`') tpl = !tpl;
        else if (!single && !dbl && !tpl && ch == target) ++count;
      }
      return count;
    };

    for (QString line : lines) {
      line = line.trimmed();
      if (line.isEmpty()) {
        if (!previousBlank && !out.isEmpty()) out << "";
        previousBlank = true;
        continue;
      }
      previousBlank = false;
      if (startsWithClosing(line)) indent = std::max(0, indent - 1);
      out << QString(indent * 2, ' ') + line;
      const int opens = countOutsideStrings(line, '{') + countOutsideStrings(line, '(') + countOutsideStrings(line, '[');
      const int closes = countOutsideStrings(line, '}') + countOutsideStrings(line, ')') + countOutsideStrings(line, ']');
      indent = std::max(0, indent + opens - closes);
    }

    while (!out.isEmpty() && out.last().isEmpty()) out.removeLast();
    return out.join('\n') + '\n';
  }

  QString newIndicatorTemplate() const {
    return R"JS(indicator("Custom Indicator", { overlay: true })

const length = input.int(20, "Length", 1, 500)
const ma = ta.ema(close, length)

plot(ma, {
  title: "EMA",
  color: "#409cff",
  width: 2
})
)JS";
  }

  QString builtinFvgCircleTemplate() const {
    return R"JS(indicator("FVG Circle", { overlay: true })

const n = input.int(1, "Left/Right bars (N)", 1, 50)
const minGapTicks = input.int(0, "Min gap ticks", 0, 10000)

function decimalPlaces(value) {
  const text = Math.abs(value).toFixed(8).replace(/0+$/, "").replace(/\.$/, "")
  const dot = text.indexOf(".")
  return dot < 0 ? 0 : text.length - dot - 1
}

let decimals = 0
const start = Math.max(0, close.length - 600)
for (let i = start; i < close.length; i++) {
  decimals = Math.max(decimals, decimalPlaces(open[i]), decimalPlaces(high[i]), decimalPlaces(low[i]), decimalPlaces(close[i]))
}
const minGap = minGapTicks * Math.pow(10, -Math.min(decimals, 8))

const marks = new Array(close.length).fill(false)

for (let current = 2 * n; current < close.length; current++) {
  let rightLow = Infinity
  let rightHigh = -Infinity
  for (let i = current - n + 1; i <= current; i++) {
    rightLow = Math.min(rightLow, low[i])
    rightHigh = Math.max(rightHigh, high[i])
  }

  let leftHigh = -Infinity
  let leftLow = Infinity
  for (let i = current - 2 * n; i <= current - n - 1; i++) {
    leftHigh = Math.max(leftHigh, high[i])
    leftLow = Math.min(leftLow, low[i])
  }

  if ((leftHigh + minGap) < rightLow || (leftLow - minGap) > rightHigh) {
    marks[current - n] = true
  }
}

plotshape(marks, {
  location: location.abovebar,
  text: "",
  color: "#409cff"
})
)JS";
  }

  void reloadIndicatorsFromDisk() {
    chart_->reloadCustomIndicators();
    loadIndicatorSettings();
    rebuildCustomIndicatorList();
    updateIndicatorErrorText();
  }

  void openIndicatorEditor(const QString &originalPath, const QString &savePath, const QString &initialCode, const QString &title) {
    Q_UNUSED(savePath);
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.setMinimumSize(820, 640);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);

    auto *pathPreview = new QLabel;
    pathPreview->setWordWrap(true);
    layout->addWidget(pathPreview);

    auto *editor = new CodeEditor;
    editor->setObjectName("debugLog");
    QFont codeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    codeFont.setPointSize(11);
    editor->setFont(codeFont);
    editor->setPlainText(formatIndicatorCode(initialCode));
    editor->applyComfortableLineSpacing();
    new JsSyntaxHighlighter(editor->document());
    editor->setTabStopDistance(QFontMetricsF(codeFont).horizontalAdvance(' ') * 2);
    layout->addWidget(editor, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto updatePreview = [this, pathPreview, editor, originalPath] {
      pathPreview->setText(QString("保存文件：%1\n文件名根据 indicator(\"名称\") 自动生成。").arg(resolvedIndicatorSavePath(editor->toPlainText(), originalPath)));
    };
    auto save = [this, &dialog, editor, originalPath] {
      const QString formatted = formatIndicatorCode(editor->toPlainText());
      editor->setPlainText(formatted);
      editor->applyComfortableLineSpacing();
      const QString target = resolvedIndicatorSavePath(formatted, originalPath);
      if (target.isEmpty()) return;
      QFile file(target);
      if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::warning(&dialog, "保存失败", QString("无法写入：%1").arg(target));
        return;
      }
      file.write(formatted.toUtf8());
      file.close();
      if (!originalPath.isEmpty() && QFileInfo(originalPath).absoluteFilePath() != QFileInfo(target).absoluteFilePath()) {
        QFile::remove(originalPath);
      }
      dialog.accept();
      reloadIndicatorsFromDisk();
    };
    connect(editor, &QPlainTextEdit::textChanged, &dialog, updatePreview);
    connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked, &dialog, save);
    auto *saveShortcut = new QShortcut(QKeySequence::Save, &dialog);
    connect(saveShortcut, &QShortcut::activated, &dialog, save);
    updatePreview();
    dialog.exec();
  }

  void deleteIndicatorScript(const IndicatorScript &script) {
    if (QMessageBox::question(indicatorDialog_, "删除指标", QString("删除 %1？\n%2").arg(script.name, script.path)) != QMessageBox::Yes) return;
    if (!QFile::remove(script.path)) {
      QMessageBox::warning(indicatorDialog_, "删除失败", QString("无法删除：%1").arg(script.path));
      return;
    }
    reloadIndicatorsFromDisk();
  }

  void buildBackendDialog() {
    backendDialog_ = new QDialog(this);
    backendDialog_->setWindowTitle("后端接口");
    backendDialog_->setMinimumSize(520, 260);
    auto *layout = new QVBoxLayout(backendDialog_);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(14);
    auto *form = new QFormLayout;
    form->setSpacing(14);
    backendUrl_ = new QLineEdit(client_.backendBase());
    wsUrl_ = new QLineEdit(client_.wsBase());
    realtime_ = new QCheckBox("启用实时 K 线");
    realtime_->setChecked(client_.realtimeEnabled());
    backendUrl_->setMinimumHeight(34);
    wsUrl_->setMinimumHeight(34);
    backendUrl_->setPlaceholderText("http://127.0.0.1:8080");
    wsUrl_->setPlaceholderText("留空则从 HTTP 地址自动推导");
    form->addRow("HTTP 后端", backendUrl_);
    form->addRow("WebSocket", wsUrl_);
    form->addRow("实时", realtime_);
    layout->addLayout(form);

    auto *hint = new QLabel("HTTP 地址用于 /api/candles；关闭实时 K 线后不会连接 /ws/candles。");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, backendDialog_, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, backendDialog_, &QDialog::accept);
  }

  bool showBackendDialog(bool startup = false) {
    if (!backendDialog_) return false;
    backendUrl_->setText(client_.backendBase());
    wsUrl_->setText(client_.wsBase());
    realtime_->setChecked(client_.realtimeEnabled());
    while (true) {
      if (backendDialog_->exec() != QDialog::Accepted) {
        return false;
      }
      const QString backend = backendUrl_->text().trimmed();
      if (backend.isEmpty()) {
        QMessageBox::warning(this, "后端接口", "HTTP 后端地址不能为空。");
        continue;
      }
      client_.configureBackend(backend, wsUrl_->text(), realtime_->isChecked());
      saveBackendSettings();
      client_.loadOverlayStrategies();
      if (!startup) refresh();
      return true;
    }
  }

  void loadBackendSettings() {
    QSettings settings("Q4J", "KLineViewer");
    const QString backend = settings.value("backend/http").toString().trimmed();
    if (backend.isEmpty()) return;
    client_.configureBackend(
      backend,
      settings.value("backend/ws").toString(),
      settings.value("backend/realtime", true).toBool()
    );
  }

  void saveBackendSettings() const {
    QSettings settings("Q4J", "KLineViewer");
    settings.setValue("backend/http", client_.backendBase());
    settings.setValue("backend/ws", client_.wsBase());
    settings.setValue("backend/realtime", client_.realtimeEnabled());
  }

  QString selectedStrategyName() const {
    if (!strategy_) return "n_in_range_variant";
    const QString text = strategy_->currentText().trimmed();
    return text.isEmpty() ? QString("n_in_range_variant") : text;
  }

  void setSelectedStrategyName(const QString &name) {
    if (!strategy_) return;
    const QString resolved = name.trimmed().isEmpty() ? QString("n_in_range_variant") : name.trimmed();
    const int index = strategy_->findText(resolved, Qt::MatchFixedString);
    if (index >= 0) strategy_->setCurrentIndex(index);
    else strategy_->setEditText(resolved);
  }

  void populateStrategies(const QStringList &strategies) {
    if (!strategy_) return;
    const QString current = selectedStrategyName();
    QSignalBlocker blocker(strategy_);
    strategy_->clear();
    QStringList values = strategies;
    if (!values.contains("n_in_range_variant", Qt::CaseInsensitive)) values.prepend("n_in_range_variant");
    values.removeDuplicates();
    values.sort(Qt::CaseInsensitive);
    strategy_->addItems(values);
    if (auto *completer = strategy_->completer()) completer->setModel(strategy_->model());
    setSelectedStrategyName(current);
  }

  void loadStrategySettings() {
    QSettings settings("Q4J", "KLineViewer");
    setSelectedStrategyName(settings.value("strategy/name", "n_in_range_variant").toString());
  }

  void saveStrategySettings() const {
    QSettings settings("Q4J", "KLineViewer");
    settings.setValue("strategy/name", selectedStrategyName());
  }

  void populateTimeZones() {
    if (!timeZone_) return;
    timeZone_->clear();
    QList<QByteArray> ids = QTimeZone::availableTimeZoneIds();
    std::sort(ids.begin(), ids.end());
    const QByteArray systemId = QTimeZone::systemTimeZoneId();
    timeZone_->addItem(QString("System (%1)").arg(QString::fromUtf8(systemId)), systemId);
    timeZone_->addItem("UTC", QByteArray("UTC"));
    for (const QByteArray &id : ids) {
      if (id == "UTC" || id == systemId) continue;
      timeZone_->addItem(QString::fromUtf8(id), id);
    }
  }

  QByteArray selectedTimeZoneId() const {
    if (!timeZone_) return QTimeZone::systemTimeZoneId();
    const QByteArray id = timeZone_->currentData().toByteArray();
    return QTimeZone(id).isValid() ? id : QTimeZone::systemTimeZoneId();
  }

  void setSelectedTimeZoneId(const QByteArray &id) {
    if (!timeZone_) return;
    const QByteArray validId = QTimeZone(id).isValid() ? id : QTimeZone::systemTimeZoneId();
    const int index = timeZone_->findData(validId);
    if (index >= 0) timeZone_->setCurrentIndex(index);
  }

  QString formatDisplayTime(qint64 ms, const QString &format) const {
    QTimeZone zone(timeZoneId_);
    if (!zone.isValid()) zone = QTimeZone::systemTimeZone();
    return QDateTime::fromMSecsSinceEpoch(ms, zone).toString(format);
  }

  void applyTimeZoneSetting() {
    timeZoneId_ = selectedTimeZoneId();
    chart_->setTimeZoneId(timeZoneId_);
    syncReplayBounds();
    if (replayActive_) applyReplayView();
    updateVisibleRangeLabel();
  }

  void loadDisplaySettings() {
    QSettings settings("Q4J", "KLineViewer");
    timeZoneId_ = settings.value("display/timeZone", QTimeZone::systemTimeZoneId()).toByteArray();
    if (!QTimeZone(timeZoneId_).isValid()) timeZoneId_ = QTimeZone::systemTimeZoneId();
    setSelectedTimeZoneId(timeZoneId_);
    chart_->setTimeZoneId(timeZoneId_);
  }

  void saveDisplaySettings() const {
    QSettings settings("Q4J", "KLineViewer");
    settings.setValue("display/timeZone", timeZoneId_);
  }

  void updateVisibleRangeLabel() {
    if (lastRangeStartMs_ <= 0 || lastRangeEndMs_ <= 0 || lastRangeFirstIndex_ < 0 || lastRangeLastIndex_ < 0) {
      range_->setText("Visible Range --");
      return;
    }
    const QString start = formatDisplayTime(lastRangeStartMs_, "yyyy-MM-dd HH:mm");
    const QString end = formatDisplayTime(lastRangeEndMs_, "yyyy-MM-dd HH:mm");
    range_->setText(QString("Visible Range  %1 → %2  [%3-%4]").arg(start, end).arg(lastRangeFirstIndex_).arg(lastRangeLastIndex_));
  }

  void loadIndicatorSettings() {
    QSettings settings("Q4J", "KLineViewer");
    const FvgCircleSettings current = chart_->fvgCircleSettings();
    const bool enabled = settings.value("indicator/fvgCircle/enabled", current.enabled).toBool();
    const int n = settings.value("indicator/fvgCircle/leftRightBars", current.leftRightBars).toInt();
    const int minGapTicks = settings.value("indicator/fvgCircle/minGapTicks", current.minGapTicks).toInt();
    chart_->setFvgCircleSettings(enabled, n, minGapTicks);
    for (const IndicatorScript &script : chart_->customIndicators()) {
      chart_->setCustomIndicatorEnabled(script.id, settings.value("indicator/script/" + script.id + "/enabled", script.enabled).toBool());
      settings.beginGroup("indicator/script/" + script.id + "/inputs");
      for (const QString &key : settings.childKeys()) {
        chart_->setCustomIndicatorInputValue(script.id, key, settings.value(key));
      }
      settings.endGroup();
    }
    if (fvgCircleEnabled_) fvgCircleEnabled_->setChecked(enabled);
    if (fvgCircleN_) fvgCircleN_->setValue(std::clamp(n, 1, 50));
    if (fvgCircleMinGapTicks_) fvgCircleMinGapTicks_->setValue(std::clamp(minGapTicks, 0, 10000));
    rebuildCustomIndicatorList();
  }

  void saveIndicatorSettings() const {
    QSettings settings("Q4J", "KLineViewer");
    const FvgCircleSettings fvg = chart_->fvgCircleSettings();
    settings.setValue("indicator/fvgCircle/enabled", fvg.enabled);
    settings.setValue("indicator/fvgCircle/leftRightBars", fvg.leftRightBars);
    settings.setValue("indicator/fvgCircle/minGapTicks", fvg.minGapTicks);
    for (const IndicatorScript &script : chart_->customIndicators()) {
      settings.setValue("indicator/script/" + script.id + "/enabled", script.enabled);
      settings.beginGroup("indicator/script/" + script.id + "/inputs");
      const QVariantHash values = chart_->customIndicatorInputValues(script.id);
      for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        settings.setValue(it.key(), it.value());
      }
      settings.endGroup();
    }
  }

  void bindSignals() {
    connect(refresh_, &QPushButton::clicked, this, &MainWindow::refresh);
    connect(symbol_, &QLineEdit::returnPressed, this, &MainWindow::refresh);
    connect(minimize_, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(maximize_, &QPushButton::clicked, this, &MainWindow::toggleMaximized);
    connect(close_, &QPushButton::clicked, this, &QWidget::close);
    connect(annotationGroup_, &QButtonGroup::idClicked, this, [this](int id) {
      chart_->setAnnotationTool(static_cast<ChartWidget::AnnotationTool>(id));
    });
    connect(chart_, &ChartWidget::annotationToolChanged, this, [this](ChartWidget::AnnotationTool tool) {
      if (QAbstractButton *button = annotationGroup_->button(static_cast<int>(tool))) button->setChecked(true);
    });
    connect(chart_, &ChartWidget::fvgCircleVisibilityChanged, this, [this](bool enabled) {
      if (fvgCircleEnabled_ && fvgCircleEnabled_->isChecked() != enabled) fvgCircleEnabled_->setChecked(enabled);
      saveIndicatorSettings();
    });
    connect(chart_, &ChartWidget::customIndicatorVisibilityChanged, this, [this](const QString &, bool) {
      saveIndicatorSettings();
      rebuildCustomIndicatorList();
      updateIndicatorErrorText();
    });
    connect(magnetButton_, &QPushButton::toggled, chart_, &ChartWidget::setMagnetEnabled);
    connect(backend_, &QPushButton::clicked, this, [this] {
      showBackendDialog(false);
    });
    connect(wsLogButton_, &QPushButton::clicked, this, [this] {
      if (debugDialog_) debugDialog_->show();
      if (debugDialog_) debugDialog_->raise();
      if (debugDialog_) debugDialog_->activateWindow();
    });
    connect(updateButton_, &QPushButton::clicked, this, &MainWindow::checkForUpdates);
    connect(theme_, &QPushButton::clicked, this, [this] {
      dark_ = !dark_;
      applyTheme();
    });
    replayTimer_.setInterval(650);
    connect(&replayTimer_, &QTimer::timeout, this, &MainWindow::stepReplay);
    connect(replayToggle_, &QPushButton::toggled, this, [this](bool enabled) {
      replayActive_ = enabled;
      if (!enabled) {
        replayTimer_.stop();
        if (replayPlay_) replayPlay_->setChecked(false);
      }
      applyReplayView();
    });
    connect(replayTime_, &QDateTimeEdit::dateTimeChanged, this, [this](const QDateTime &value) {
      replayTimeTouched_ = true;
      replayLoadCursorMs_ = 0;
      if (!replayStepping_ && replayPlay_ && replayPlay_->isChecked()) replayPlay_->setChecked(false);
      const QDateTime normalized = normalizeReplayMinute(value);
      if (normalized != value) {
        QSignalBlocker blocker(replayTime_);
        replayTime_->setDateTime(normalized);
      }
      if (replayActive_) applyReplayView();
    });
    connect(replayPlay_, &QPushButton::toggled, this, [this](bool playing) {
      if (playing) {
        if (!replayActive_ && replayToggle_) replayToggle_->setChecked(true);
        replayTimer_.start();
      } else {
        replayTimer_.stop();
      }
    });
    connect(replayStep_, &QPushButton::clicked, this, [this] {
      if (!replayActive_ && replayToggle_) replayToggle_->setChecked(true);
      stepReplay();
    });
    connect(timeZone_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
      applyTimeZoneSetting();
      saveDisplaySettings();
    });
    connect(settings_, &QPushButton::clicked, this, [this] {
      client_.loadOverlayStrategies();
      settingsDialog_->show();
    });
    connect(indicators_, &QPushButton::clicked, this, [this] {
      rebuildCustomIndicatorList();
      updateIndicatorErrorText();
      indicatorDialog_->show();
    });
    connect(&client_, &CandleClient::candlesLoaded, this, [this](const QVector<Candle> &candles) {
      loadedCandles_ = candles;
      normalizeLoadedCandles();
      syncReplayBounds();
      applyReplayView();
      updateIndicatorErrorText();
    });
    connect(&client_, &CandleClient::olderCandlesLoaded, this, [this](const QVector<Candle> &candles) {
      loadedCandles_ += candles;
      normalizeLoadedCandles();
      syncReplayBounds();
      applyReplayView();
      updateIndicatorErrorText();
    });
    connect(&client_, &CandleClient::replayCandlesLoaded, this, [this](const QVector<Candle> &candles) {
      loadedCandles_ += candles;
      normalizeLoadedCandles();
      replayLoadCursorMs_ = 0;
      applyReplayView();
      updateIndicatorErrorText();
    });
    connect(&client_, &CandleClient::overlayEventsLoaded, chart_, &ChartWidget::setOverlayEvents);
    connect(&client_, &CandleClient::overlayEventsLoaded, this, [this](const QJsonValue &events) {
      int count = 0;
      if (events.isArray()) count = events.toArray().size();
      else if (events.isObject()) count = events.toObject().value("layers").toArray().size();
      events_->setText(QString("Events %1").arg(count));
    });
    connect(&client_, &CandleClient::overlayStrategiesLoaded, this, &MainWindow::populateStrategies);
    connect(&client_, &CandleClient::candleUpdated, this, [this](const Candle &candle) {
      appendDebugLog(QString("Chart upsert candle: %1 O=%2 H=%3 L=%4 C=%5")
        .arg(QDateTime::fromMSecsSinceEpoch(candle.ms).toString("yyyy-MM-dd HH:mm:ss.zzz"))
        .arg(candle.open)
        .arg(candle.high)
        .arg(candle.low)
        .arg(candle.close));
      upsertLoadedCandle(candle);
      if (!replayActive_) chart_->upsertCandle(candle);
      updateIndicatorErrorText();
    });
    connect(&client_, &CandleClient::debugLog, this, &MainWindow::appendDebugLog);
    connect(&client_, &CandleClient::errorMessage, this, [this](const QString &message) {
      if (message.trimmed().isEmpty()) chart_->clearMessage();
      else chart_->showMessage(message.trimmed());
    });
    connect(&client_, &CandleClient::loadFailed, this, [this](const QString &message) {
      chart_->showLoadError(message.trimmed().isEmpty() ? "加载失败" : message.trimmed());
      events_->setText("Events --");
      range_->setText("Visible Range --");
      ohlc_->setText("O -- H -- L -- C --");
    });
    connect(chart_, &ChartWidget::olderCandlesRequested, &client_, &CandleClient::loadOlder);
    connect(chart_, &ChartWidget::overlayRangeChanged, &client_, &CandleClient::loadOverlayRange);
    connect(chart_, &ChartWidget::visibleRangeChanged, this, [this](qint64 startMs, qint64 endMs, int firstIndex, int lastIndex) {
      lastRangeStartMs_ = startMs;
      lastRangeEndMs_ = endMs;
      lastRangeFirstIndex_ = firstIndex;
      lastRangeLastIndex_ = lastIndex;
      if (startMs <= 0 || endMs <= 0 || firstIndex < 0 || lastIndex < 0) {
        range_->setText("Visible Range --");
        return;
      }
      updateVisibleRangeLabel();
    });
    connect(&client_, &CandleClient::statusChanged, this, [this](const QString &status, bool live) {
      status_->setText((live ? "● " : "○ ") + status);
    });
    connect(chart_, &ChartWidget::hoveredCandleChanged, this, [this](const Candle *c) {
      if (!c) {
        ohlc_->setText("O -- H -- L -- C --");
        return;
      }
      ohlc_->setText(QString("O %1  H %2  L %3  C %4").arg(c->open).arg(c->high).arg(c->low).arg(c->close));
    });
  }

  void applyTheme() {
    chart_->setDark(dark_);
    theme_->setText(dark_ ? "☾" : "☀");
    if (maximize_) maximize_->setText((maximizedAnimated_ || isMaximized()) ? "❐" : "□");
    const QString css = dark_ ? darkCss() : lightCss();
    qApp->setStyleSheet(css);
  }

  void toggleMaximized() {
    const bool restoring = maximizedAnimated_ || isMaximized();
    if (restoring) {
      if (normalGeometry_.isValid()) setGeometry(normalGeometry_);
      showNormal();
      maximizedAnimated_ = false;
    } else {
      normalGeometry_ = geometry();
      const QRect target = screen() ? screen()->availableGeometry() : QApplication::primaryScreen()->availableGeometry();
      setGeometry(target);
      maximizedAnimated_ = true;
    }
    if (maximize_) maximize_->setText(maximizedAnimated_ ? "❐" : "□");
  }

  QString darkCss() const {
    return R"(
      QWidget { background: #080c0b; color: #f4efe3; font-family: "Microsoft YaHei UI", "Segoe UI", "PingFang SC", "Noto Sans CJK SC"; font-size: 13px; }
      QMainWindow { background: #080c0b; }
      QWidget#appShell {
        background: #080c0b;
        border: 1px solid rgba(230, 226, 211, 48);
      }
      QFrame#titleBar {
        min-height: 32px; max-height: 32px;
        background: #101612;
        border: 1px solid rgba(230, 226, 211, 42);
        border-radius: 3px;
      }
      QLabel#titleBadge {
        min-width: 30px; max-width: 30px; min-height: 20px; max-height: 20px;
        background: #f0b64f;
        color: #111813;
        font-size: 11px;
        font-weight: 500;
        qproperty-alignment: AlignCenter;
      }
      QLabel#titleText {
        background: transparent;
        color: #f4efe3;
        font-size: 14px;
        font-weight: 500;
      }
      QPushButton#windowButton, QPushButton#closeButton {
        min-height: 24px; max-height: 24px;
        background: transparent;
        border: 1px solid transparent;
        border-radius: 2px;
        padding: 0;
        color: #a7b0a8;
        font-size: 15px;
        font-weight: 500;
      }
      QPushButton#windowButton:hover { background: rgba(230, 226, 211, 22); border-color: rgba(230, 226, 211, 44); color: #f4efe3; }
      QPushButton#closeButton:hover { background: #ef5f78; border-color: #ef5f78; color: #111813; }
      QPushButton#macCloseButton, QPushButton#macMinimizeButton, QPushButton#macMaximizeButton {
        min-width: 13px; max-width: 13px; min-height: 13px; max-height: 13px;
        border-radius: 6px;
        border: 1px solid rgba(0, 0, 0, 70);
        padding: 0;
      }
      QPushButton#macCloseButton { background: #ff5f57; }
      QPushButton#macMinimizeButton { background: #febc2e; }
      QPushButton#macMaximizeButton { background: #28c840; }
      QPushButton#macCloseButton:hover, QPushButton#macMinimizeButton:hover, QPushButton#macMaximizeButton:hover {
        border-color: rgba(255, 255, 255, 130);
      }
      QFrame#header, QFrame#footer {
        background: #0e1311;
        border: 1px solid rgba(230, 226, 211, 34);
        border-radius: 3px;
      }
      QWidget#chartRow { background: transparent; }
      QFrame#annotationToolbar {
        background: #0e1311;
        border: 1px solid rgba(230, 226, 211, 34);
        border-radius: 3px;
      }
      QFrame#annotationDivider {
        background: rgba(230, 226, 211, 34);
        border: 0;
      }
      QFrame#annotationFloatingToolbar {
        background: rgba(14, 19, 17, 232);
        border: 1px solid rgba(230, 226, 211, 48);
        border-radius: 3px;
      }
      QFrame#annotationFloatingToolbar QSpinBox {
        min-height: 22px; max-height: 22px;
        background: #151d19;
        border: 1px solid rgba(230, 226, 211, 44);
        color: #f4efe3;
        padding: 0 4px;
      }
      QPushButton#annotationToolButton {
        min-width: 34px; max-width: 34px; min-height: 34px; max-height: 34px;
        background: transparent;
        border: 1px solid rgba(230, 226, 211, 30);
        border-radius: 2px;
        padding: 0;
        color: #d9d4c7;
        font-size: 15px;
        font-weight: 500;
      }
      QPushButton#annotationToolButton:hover {
        background: #18211d;
        border-color: rgba(240, 182, 79, 145);
      }
      QPushButton#annotationToolButton:checked {
        background: rgba(240, 182, 79, 42);
        border-color: #f0b64f;
        color: #f0b64f;
      }
      QWidget#brandBox { background: transparent; }
      QLabel#brandBadge {
        min-width: 34px; max-width: 34px; min-height: 34px; max-height: 34px;
        border: 1px solid rgba(240, 182, 79, 150);
        border-radius: 2px;
        background: #22251a;
        color: #f0b64f;
        font-size: 11px;
        font-weight: 500;
        qproperty-alignment: AlignCenter;
      }
      QLabel#brandText {
        background: transparent;
        color: #f4efe3;
        font-size: 18px;
        font-weight: 500;
      }
      QLineEdit, QComboBox, QPushButton {
        min-height: 30px; max-height: 30px;
        background: #1f2723;
        border: 1px solid rgba(230, 226, 211, 64);
        border-radius: 2px;
        padding: 0 8px;
        font-weight: 500;
      }
      QLineEdit#symbolInput { font-size: 14px; font-weight: 500; }
      QLineEdit#symbolInput {
        min-height: 28px; max-height: 28px;
        background: #111815;
        border: 1px solid rgba(230, 226, 211, 42);
        border-left: 2px solid #f0b64f;
        border-radius: 1px;
        padding: 0 9px;
        color: #f4efe3;
        selection-background-color: #f0b64f;
        selection-color: #111813;
      }
      QComboBox#intervalInput {
        min-height: 28px; max-height: 28px;
        background: #151d19;
        border: 1px solid rgba(230, 226, 211, 38);
        border-radius: 1px;
        padding: 0 18px 0 9px;
        color: #f4efe3;
        font-size: 13px;
        font-weight: 500;
      }
      QComboBox#intervalInput::drop-down {
        width: 16px;
        border: 0;
        background: transparent;
      }
      QComboBox#intervalInput::down-arrow {
        image: none;
        width: 0;
        height: 0;
      }
      QPushButton#toolButton {
        min-height: 28px; max-height: 28px;
        background: transparent;
        border: 1px solid rgba(230, 226, 211, 38);
        border-radius: 1px;
        padding: 0 9px;
        color: #d9d4c7;
        font-size: 13px;
        font-weight: 500;
      }
      QPushButton#toolButton:hover, QComboBox#intervalInput:hover, QLineEdit#symbolInput:hover {
        background: #18211d;
        border-color: rgba(240, 182, 79, 150);
      }
      QLineEdit#symbolInput:focus, QComboBox#intervalInput:focus, QPushButton#toolButton:focus {
        border-color: #f0b64f;
      }
      QDateTimeEdit#replayTime {
        min-height: 28px; max-height: 28px;
        background: #151d19;
        border: 1px solid rgba(230, 226, 211, 38);
        border-radius: 1px;
        padding: 0 8px;
        color: #f4efe3;
        font-size: 12px;
        font-weight: 500;
      }
      QDateTimeEdit#replayTime:hover, QDateTimeEdit#replayTime:focus {
        background: #18211d;
        border-color: rgba(240, 182, 79, 150);
      }
      QPushButton#iconButton {
        min-height: 28px; max-height: 28px;
        background: transparent;
        border: 1px solid rgba(230, 226, 211, 34);
        border-radius: 1px;
        padding: 0;
        color: #f0b64f;
        font-size: 15px;
        font-weight: 500;
      }
      QPushButton#refreshButton { background: #f0b64f; border-color: #f0b64f; color: #111813; }
      QPushButton#refreshButton {
        min-height: 28px; max-height: 28px;
        border-radius: 1px;
        padding: 0 8px;
        font-size: 13px;
        font-weight: 500;
      }
      QPushButton:hover, QLineEdit:focus, QComboBox:focus { border-color: #f0b64f; }
      QLabel#status {
        background: transparent;
        color: #a7b0a8;
        font-size: 13px;
        font-weight: 500;
        qproperty-alignment: AlignCenter;
      }
      QFrame#footer QLabel {
        background: transparent;
        font-size: 13px;
        font-weight: 500;
      }
      QComboBox#footerTimeZone {
        min-height: 24px; max-height: 24px;
        background: #111815;
        border: 1px solid rgba(230, 226, 211, 34);
        border-radius: 1px;
        padding: 0 18px 0 8px;
        color: #d9d4c7;
        font-size: 12px;
        font-weight: 500;
      }
      QComboBox#footerTimeZone:hover, QComboBox#footerTimeZone:focus {
        border-color: rgba(240, 182, 79, 150);
      }
      QComboBox#footerTimeZone::drop-down {
        width: 16px;
        border: 0;
        background: transparent;
      }
      QComboBox#footerTimeZone::down-arrow {
        image: none;
        width: 0;
        height: 0;
      }
      QDialog QWidget {
        background: transparent;
      }
      QDialog QLabel, QDialog QCheckBox, QDialog QDialogButtonBox {
        background: transparent;
        font-size: 13px;
        font-weight: 400;
      }
      QDialog QLabel#dialogTitle {
        color: #f4efe3;
        font-size: 18px;
        font-weight: 600;
      }
      QDialog QLabel#sectionLabel {
        color: #f0b64f;
        font-size: 13px;
        font-weight: 600;
      }
      QDialog QLabel#mutedLabel {
        color: #a7b0a8;
        font-size: 12px;
      }
      QWidget#indicatorRow {
        background: rgba(230, 226, 211, 13);
        border: 1px solid rgba(230, 226, 211, 28);
        border-radius: 3px;
      }
      QWidget#indicatorParams {
        background: rgba(230, 226, 211, 8);
        border-left: 1px solid rgba(240, 182, 79, 90);
      }
      QSplitter::handle {
        background: rgba(230, 226, 211, 24);
        width: 1px;
      }
      QDialog QLineEdit, QDialog QComboBox {
        background: #151d19;
        border: 1px solid rgba(230, 226, 211, 48);
        color: #f4efe3;
      }
      QDialog QPlainTextEdit {
        background: #0a0f0d;
        border: 1px solid rgba(230, 226, 211, 42);
        color: #f4efe3;
        font-family: "Cascadia Mono", "Consolas", "Microsoft YaHei UI";
      }
      QDialog {
        background: #0e1311;
        border: 1px solid rgba(230, 226, 211, 34);
      }
    )";
  }

  QString lightCss() const {
    return R"(
      QWidget { background: #eef0eb; color: #131916; font-family: "Microsoft YaHei UI", "Segoe UI", "PingFang SC", "Noto Sans CJK SC"; font-size: 13px; }
      QMainWindow { background: #eef0eb; }
      QWidget#appShell {
        background: #eef0eb;
        border: 1px solid rgba(23, 31, 27, 48);
      }
      QFrame#titleBar {
        min-height: 32px; max-height: 32px;
        background: #fbfaf4;
        border: 1px solid rgba(23, 31, 27, 42);
        border-radius: 3px;
      }
      QLabel#titleBadge {
        min-width: 30px; max-width: 30px; min-height: 20px; max-height: 20px;
        background: #f0b64f;
        color: #111813;
        font-size: 11px;
        font-weight: 500;
        qproperty-alignment: AlignCenter;
      }
      QLabel#titleText {
        background: transparent;
        color: #131916;
        font-size: 14px;
        font-weight: 500;
      }
      QPushButton#windowButton, QPushButton#closeButton {
        min-height: 24px; max-height: 24px;
        background: transparent;
        border: 1px solid transparent;
        border-radius: 2px;
        padding: 0;
        color: #59635d;
        font-size: 15px;
        font-weight: 500;
      }
      QPushButton#windowButton:hover { background: rgba(23, 31, 27, 22); border-color: rgba(23, 31, 27, 44); color: #131916; }
      QPushButton#closeButton:hover { background: #ef5f78; border-color: #ef5f78; color: #111813; }
      QPushButton#macCloseButton, QPushButton#macMinimizeButton, QPushButton#macMaximizeButton {
        min-width: 13px; max-width: 13px; min-height: 13px; max-height: 13px;
        border-radius: 6px;
        border: 1px solid rgba(0, 0, 0, 70);
        padding: 0;
      }
      QPushButton#macCloseButton { background: #ff5f57; }
      QPushButton#macMinimizeButton { background: #febc2e; }
      QPushButton#macMaximizeButton { background: #28c840; }
      QPushButton#macCloseButton:hover, QPushButton#macMinimizeButton:hover, QPushButton#macMaximizeButton:hover {
        border-color: rgba(0, 0, 0, 110);
      }
      QFrame#header, QFrame#footer {
        background: #fffdf7;
        border: 1px solid rgba(23, 31, 27, 34);
        border-radius: 3px;
      }
      QWidget#chartRow { background: transparent; }
      QFrame#annotationToolbar {
        background: #fffdf7;
        border: 1px solid rgba(23, 31, 27, 34);
        border-radius: 3px;
      }
      QFrame#annotationDivider {
        background: rgba(23, 31, 27, 34);
        border: 0;
      }
      QFrame#annotationFloatingToolbar {
        background: rgba(255, 253, 247, 235);
        border: 1px solid rgba(23, 31, 27, 42);
        border-radius: 3px;
      }
      QFrame#annotationFloatingToolbar QSpinBox {
        min-height: 22px; max-height: 22px;
        background: #ffffff;
        border: 1px solid rgba(23, 31, 27, 36);
        color: #131916;
        padding: 0 4px;
      }
      QPushButton#annotationToolButton {
        min-width: 34px; max-width: 34px; min-height: 34px; max-height: 34px;
        background: transparent;
        border: 1px solid rgba(23, 31, 27, 30);
        border-radius: 2px;
        padding: 0;
        color: #3b443e;
        font-size: 15px;
        font-weight: 500;
      }
      QPushButton#annotationToolButton:hover {
        background: #ffffff;
        border-color: rgba(178, 122, 23, 145);
      }
      QPushButton#annotationToolButton:checked {
        background: rgba(240, 182, 79, 50);
        border-color: #b27a17;
        color: #b27a17;
      }
      QWidget#brandBox { background: transparent; }
      QLabel#brandBadge {
        min-width: 34px; max-width: 34px; min-height: 34px; max-height: 34px;
        border: 1px solid rgba(184, 125, 24, 170);
        border-radius: 2px;
        background: #f4ecd9;
        color: #b27a17;
        font-size: 11px;
        font-weight: 500;
        qproperty-alignment: AlignCenter;
      }
      QLabel#brandText {
        background: transparent;
        color: #131916;
        font-size: 18px;
        font-weight: 500;
      }
      QLineEdit, QComboBox, QPushButton {
        min-height: 30px; max-height: 30px;
        background: #eef1ea;
        border: 1px solid rgba(23, 31, 27, 62);
        border-radius: 2px;
        padding: 0 8px;
        font-weight: 500;
      }
      QLineEdit#symbolInput { font-size: 14px; font-weight: 500; }
      QLineEdit#symbolInput {
        min-height: 28px; max-height: 28px;
        background: #fffdf7;
        border: 1px solid rgba(23, 31, 27, 40);
        border-left: 2px solid #b27a17;
        border-radius: 1px;
        padding: 0 9px;
        color: #131916;
        selection-background-color: #f0b64f;
        selection-color: #111813;
      }
      QComboBox#intervalInput {
        min-height: 28px; max-height: 28px;
        background: #f7f8f2;
        border: 1px solid rgba(23, 31, 27, 38);
        border-radius: 1px;
        padding: 0 18px 0 9px;
        color: #131916;
        font-size: 13px;
        font-weight: 500;
      }
      QComboBox#intervalInput::drop-down {
        width: 16px;
        border: 0;
        background: transparent;
      }
      QComboBox#intervalInput::down-arrow {
        image: none;
        width: 0;
        height: 0;
      }
      QPushButton#toolButton {
        min-height: 28px; max-height: 28px;
        background: transparent;
        border: 1px solid rgba(23, 31, 27, 38);
        border-radius: 1px;
        padding: 0 9px;
        color: #3b443e;
        font-size: 13px;
        font-weight: 500;
      }
      QPushButton#toolButton:hover, QComboBox#intervalInput:hover, QLineEdit#symbolInput:hover {
        background: #ffffff;
        border-color: rgba(178, 122, 23, 150);
      }
      QLineEdit#symbolInput:focus, QComboBox#intervalInput:focus, QPushButton#toolButton:focus {
        border-color: #b27a17;
      }
      QDateTimeEdit#replayTime {
        min-height: 28px; max-height: 28px;
        background: #f7f8f2;
        border: 1px solid rgba(23, 31, 27, 38);
        border-radius: 1px;
        padding: 0 8px;
        color: #131916;
        font-size: 12px;
        font-weight: 500;
      }
      QDateTimeEdit#replayTime:hover, QDateTimeEdit#replayTime:focus {
        background: #ffffff;
        border-color: rgba(178, 122, 23, 150);
      }
      QPushButton#iconButton {
        min-height: 28px; max-height: 28px;
        background: transparent;
        border: 1px solid rgba(23, 31, 27, 34);
        border-radius: 1px;
        padding: 0;
        color: #b27a17;
        font-size: 15px;
        font-weight: 500;
      }
      QPushButton#refreshButton { background: #f0b64f; border-color: #d79b2f; color: #111813; }
      QPushButton#refreshButton {
        min-height: 28px; max-height: 28px;
        border-radius: 1px;
        padding: 0 8px;
        font-size: 13px;
        font-weight: 500;
      }
      QPushButton:hover, QLineEdit:focus, QComboBox:focus { border-color: #b27a17; }
      QLabel#status {
        background: transparent;
        color: #59635d;
        font-size: 13px;
        font-weight: 500;
        qproperty-alignment: AlignCenter;
      }
      QFrame#footer QLabel {
        background: transparent;
        font-size: 13px;
        font-weight: 500;
      }
      QComboBox#footerTimeZone {
        min-height: 24px; max-height: 24px;
        background: #fbfaf4;
        border: 1px solid rgba(23, 31, 27, 34);
        border-radius: 1px;
        padding: 0 18px 0 8px;
        color: #3b443e;
        font-size: 12px;
        font-weight: 500;
      }
      QComboBox#footerTimeZone:hover, QComboBox#footerTimeZone:focus {
        border-color: rgba(178, 122, 23, 150);
      }
      QComboBox#footerTimeZone::drop-down {
        width: 16px;
        border: 0;
        background: transparent;
      }
      QComboBox#footerTimeZone::down-arrow {
        image: none;
        width: 0;
        height: 0;
      }
      QDialog QWidget {
        background: transparent;
      }
      QDialog QLabel, QDialog QCheckBox, QDialog QDialogButtonBox {
        background: transparent;
        font-size: 13px;
        font-weight: 400;
      }
      QDialog QLabel#dialogTitle {
        color: #131916;
        font-size: 18px;
        font-weight: 600;
      }
      QDialog QLabel#sectionLabel {
        color: #b27a17;
        font-size: 13px;
        font-weight: 600;
      }
      QDialog QLabel#mutedLabel {
        color: #59635d;
        font-size: 12px;
      }
      QWidget#indicatorRow {
        background: rgba(23, 31, 27, 10);
        border: 1px solid rgba(23, 31, 27, 24);
        border-radius: 3px;
      }
      QWidget#indicatorParams {
        background: rgba(23, 31, 27, 6);
        border-left: 1px solid rgba(178, 122, 23, 90);
      }
      QSplitter::handle {
        background: rgba(23, 31, 27, 24);
        width: 1px;
      }
      QDialog QLineEdit, QDialog QComboBox {
        background: #ffffff;
        border: 1px solid rgba(23, 31, 27, 42);
        color: #131916;
      }
      QDialog QPlainTextEdit {
        background: #ffffff;
        border: 1px solid rgba(23, 31, 27, 34);
        color: #131916;
        font-family: "Cascadia Mono", "Consolas", "Microsoft YaHei UI";
      }
      QDialog {
        background: #fffdf7;
        border: 1px solid rgba(23, 31, 27, 34);
      }
    )";
  }

  void appendDebugLog(const QString &message) {
    if (!debugLog_) return;
    QScrollBar *bar = debugLog_->verticalScrollBar();
    const bool wasAtBottom = !bar || bar->value() >= bar->maximum() - 2;
    debugLog_->appendPlainText(message);
    if (wasAtBottom && bar) bar->setValue(bar->maximum());
  }

  void checkForUpdates() {
    const QString repo = QString::fromUtf8(Q4J_UPDATE_REPO).trimmed();
    if (repo.isEmpty()) {
      QMessageBox::information(this, "检查更新", "当前构建没有配置 GitHub 仓库，无法在线检查更新。");
      return;
    }
    updateButton_->setEnabled(false);
    updateButton_->setText("检查中");
    requestUpdateRelease(QUrl(QString("https://api.github.com/repos/%1/releases/latest").arg(repo)), true);
  }

  QNetworkRequest makeGitHubReleaseRequest(const QUrl &url) const {
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("User-Agent", "q4j-kline-viewer");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    request.setTransferTimeout(15000);
#endif
    return request;
  }

  void requestUpdateRelease(const QUrl &url, bool allowFallback) {
    QNetworkRequest request = makeGitHubReleaseRequest(url);
    QNetworkReply *reply = updateNetwork_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, allowFallback] {
      const QByteArray body = reply->readAll();
      const QUrl requestedUrl = reply->url();
      const QNetworkReply::NetworkError error = reply->error();
      const QString errorString = reply->errorString();
      const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      reply->deleteLater();
      if (error != QNetworkReply::NoError) {
        if (allowFallback) {
          requestUpdateRelease(fallbackReleaseApiUrl(), false);
          return;
        }
        finishUpdateCheck();
        showUpdateCheckFailure(errorString, httpStatus, body, requestedUrl);
        return;
      }
      if (!handleUpdateReply(body) && allowFallback) {
        requestUpdateRelease(fallbackReleaseApiUrl(), false);
        return;
      }
      finishUpdateCheck();
    });
  }

  QUrl fallbackReleaseApiUrl() const {
    const QString repo = QString::fromUtf8(Q4J_UPDATE_REPO).trimmed();
    return QUrl(QString("https://api.github.com/repos/%1/releases?per_page=1").arg(repo));
  }

  void finishUpdateCheck() {
    updateButton_->setEnabled(true);
    updateButton_->setText("检查更新");
  }

  QString updateFailureMessage(const QByteArray &body) const {
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
      const QString message = doc.object().value("message").toString().trimmed();
      if (!message.isEmpty()) return message;
    }
    const QString text = QString::fromUtf8(body).trimmed();
    return text.left(320);
  }

  void showUpdateCheckFailure(const QString &error, int httpStatus, const QByteArray &body, const QUrl &url) {
    QString detail = updateFailureMessage(body);
    QString message = error.trimmed().isEmpty() ? "网络请求失败" : error.trimmed();
    if (httpStatus > 0) message += QString(" (HTTP %1)").arg(httpStatus);
    if (!detail.isEmpty()) message += QString("\n%1").arg(detail);

    QMessageBox box(this);
    box.setWindowTitle("检查更新失败");
    box.setText(message);
    box.setInformativeText("可以稍后重试，或直接打开 GitHub Releases 页面。");
    QPushButton *open = box.addButton("打开 Releases", QMessageBox::AcceptRole);
    box.addButton("关闭", QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == open) {
      const QString repo = QString::fromUtf8(Q4J_UPDATE_REPO).trimmed();
      QDesktopServices::openUrl(QUrl(QString("https://github.com/%1/releases").arg(repo)));
    }
    appendDebugLog(QString("Update check failed: url=%1, http=%2, error=%3, body=%4")
      .arg(url.toString())
      .arg(httpStatus)
      .arg(error)
      .arg(QString::fromUtf8(body).left(1000)));
  }

  bool handleUpdateReply(const QByteArray &body) {
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonObject release;
    if (doc.isObject()) {
      release = doc.object();
    } else if (doc.isArray() && !doc.array().isEmpty() && doc.array().first().isObject()) {
      release = doc.array().first().toObject();
    } else {
      QMessageBox::warning(this, "检查更新", "GitHub Release 响应格式错误。");
      return false;
    }
    const QString latest = release.value("tag_name").toString().trimmed();
    if (latest.isEmpty()) {
      QMessageBox::warning(this, "检查更新", "没有读取到最新版本号。");
      return false;
    }
    const QString current = QString::fromUtf8(Q4J_APP_VERSION);
    if (compareVersions(latest, current) <= 0) {
      QMessageBox::information(this, "检查更新", QString("当前已是最新版本：%1").arg(current));
      return true;
    }

    QUrl downloadUrl;
    const QJsonArray assets = release.value("assets").toArray();
#ifdef Q_OS_WIN
    QUrl fallbackExeUrl;
#endif
    for (const QJsonValue &value : assets) {
      const QJsonObject asset = value.toObject();
      const QString name = asset.value("name").toString().toLower();
#ifdef Q_OS_WIN
      const QUrl assetUrl(asset.value("browser_download_url").toString());
      if (name.endsWith(".exe") && (name.contains("setup") || name.contains("installer"))) {
        downloadUrl = assetUrl;
        break;
      }
      if (fallbackExeUrl.isEmpty() && name.endsWith(".exe")) fallbackExeUrl = assetUrl;
      const bool matches = false;
#elif defined(Q_OS_MACOS)
      const bool matches = name.endsWith(".dmg");
#else
      const bool matches = name.endsWith(".appimage") || name.endsWith(".tar.gz") || name.endsWith(".zip");
#endif
      if (matches) {
        downloadUrl = QUrl(asset.value("browser_download_url").toString());
        break;
      }
    }
#ifdef Q_OS_WIN
    if (downloadUrl.isEmpty()) downloadUrl = fallbackExeUrl;
#endif
    if (downloadUrl.isEmpty()) downloadUrl = QUrl(release.value("html_url").toString());
    if (downloadUrl.isEmpty()) {
      QMessageBox::warning(this, "检查更新", QString("发现新版本 %1，但没有找到可下载文件。").arg(latest));
      return true;
    }

    QMessageBox box(this);
    box.setWindowTitle("发现新版本");
    box.setText(QString("发现新版本 %1，当前版本 %2。").arg(latest, current));
    box.setInformativeText("是否打开下载链接？");
    QPushButton *open = box.addButton("打开下载", QMessageBox::AcceptRole);
    box.addButton("取消", QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == open) QDesktopServices::openUrl(downloadUrl);
    return true;
  }

  void normalizeLoadedCandles() {
    std::sort(loadedCandles_.begin(), loadedCandles_.end(), [](const Candle &a, const Candle &b) {
      return a.ms < b.ms;
    });
    loadedCandles_.erase(std::unique(loadedCandles_.begin(), loadedCandles_.end(), [](const Candle &a, const Candle &b) {
      return a.ms == b.ms;
    }), loadedCandles_.end());
  }

  void upsertLoadedCandle(const Candle &candle) {
    const auto it = std::lower_bound(loadedCandles_.begin(), loadedCandles_.end(), candle.ms, [](const Candle &item, qint64 ms) {
      return item.ms < ms;
    });
    if (it != loadedCandles_.end() && it->ms == candle.ms) *it = candle;
    else loadedCandles_.insert(it, candle);
    if (!replayActive_) syncReplayBounds();
  }

  QDateTime normalizeReplayMinute(const QDateTime &dateTime) const {
    QDateTime normalized = dateTime;
    normalized.setTime(QTime(dateTime.time().hour(), dateTime.time().minute()));
    return normalized;
  }

  qint64 replayCursorMs() const {
    if (!replayTime_) return 0;
    QTimeZone zone(timeZoneId_);
    if (!zone.isValid()) zone = QTimeZone::systemTimeZone();
    return QDateTime(replayTime_->date(), replayTime_->time(), zone).toMSecsSinceEpoch();
  }

  QDateTime replayDateTimeFromMs(qint64 ms) const {
    QTimeZone zone(timeZoneId_);
    if (!zone.isValid()) zone = QTimeZone::systemTimeZone();
    return QDateTime::fromMSecsSinceEpoch(ms, zone);
  }

  QVector<Candle> replayCandles() const {
    if (!replayActive_) return loadedCandles_;
    const qint64 cursor = replayCursorMs();
    QVector<Candle> visible;
    visible.reserve(loadedCandles_.size());
    for (const Candle &candle : loadedCandles_) {
      if (candle.ms <= cursor) visible.push_back(candle);
      else break;
    }
    return visible;
  }

  bool hasReplayDataForCursor(qint64 cursor) const {
    if (!replayActive_) return true;
    if (loadedCandles_.isEmpty()) return false;
    return cursor >= loadedCandles_.first().ms;
  }

  bool requestReplayDataIfNeeded(qint64 cursor) {
    if (hasReplayDataForCursor(cursor)) return false;
    if (replayLoadCursorMs_ == cursor) return true;
    replayLoadCursorMs_ = cursor;
    if (chart_) chart_->showMessage("正在加载 Replay 时间附近的K线...");
    client_.loadReplayAround(cursor);
    return true;
  }

  void applyReplayView() {
    if (!chart_) return;
    if (replayActive_ && requestReplayDataIfNeeded(replayCursorMs())) {
      updateReplayUi();
      return;
    }
    chart_->setReplayCandles(replayCandles());
    updateReplayUi();
  }

  void syncReplayBounds() {
    if (!replayTime_ || loadedCandles_.isEmpty()) return;
    if (replayActive_) return;
    const QDateTime last = replayDateTimeFromMs(loadedCandles_.last().ms);
    if (replayTimeTouched_) return;
    QSignalBlocker blocker(replayTime_);
    replayTime_->setDateTime(normalizeReplayMinute(last));
  }

  void updateReplayUi() {
    if (replayToggle_) replayToggle_->setText(replayActive_ ? "退出" : "Replay");
    if (replayPlay_) replayPlay_->setText(replayTimer_.isActive() ? "Ⅱ" : "▶");
  }

  void stepReplay() {
    if (!replayTime_ || loadedCandles_.isEmpty()) return;
    const qint64 cursor = replayCursorMs();
    auto it = std::upper_bound(loadedCandles_.begin(), loadedCandles_.end(), cursor, [](qint64 ms, const Candle &candle) {
      return ms < candle.ms;
    });
    if (it == loadedCandles_.end()) {
      replayTimer_.stop();
      if (replayPlay_) replayPlay_->setChecked(false);
      updateReplayUi();
      return;
    }
    replayStepping_ = true;
    replayTime_->setDateTime(replayDateTimeFromMs(it->ms));
    replayStepping_ = false;
    if (replayActive_) applyReplayView();
  }

  void refresh() {
    events_->setText("Events --");
    range_->setText("Visible Range --");
    lastRangeStartMs_ = 0;
    lastRangeEndMs_ = 0;
    lastRangeFirstIndex_ = -1;
    lastRangeLastIndex_ = -1;
    loadedCandles_.clear();
    replayLoadCursorMs_ = 0;
    replayTimer_.stop();
    if (replayPlay_) replayPlay_->setChecked(false);
    chart_->clearMessage();
    chart_->setCandles({});
    chart_->setOverlayEvents(QJsonArray{});
    client_.load(symbol_->text(), interval_->currentText(), selectedStrategyName(), higher_->currentText(), lower_->currentText());
  }

  ChartWidget *chart_ = nullptr;
  QFrame *titleBar_ = nullptr;
  QFrame *header_ = nullptr;
  QFrame *footer_ = nullptr;
  QFrame *annotationToolbar_ = nullptr;
  QButtonGroup *annotationGroup_ = nullptr;
  QLineEdit *symbol_ = nullptr;
  QComboBox *interval_ = nullptr;
  QComboBox *strategy_ = nullptr;
  QComboBox *higher_ = nullptr;
  QComboBox *lower_ = nullptr;
  QComboBox *timeZone_ = nullptr;
  QDateTimeEdit *replayTime_ = nullptr;
  QPushButton *settings_ = nullptr;
  QPushButton *indicators_ = nullptr;
  QPushButton *replayToggle_ = nullptr;
  QPushButton *replayPlay_ = nullptr;
  QPushButton *replayStep_ = nullptr;
  QPushButton *backend_ = nullptr;
  QPushButton *wsLogButton_ = nullptr;
  QPushButton *updateButton_ = nullptr;
  QPushButton *magnetButton_ = nullptr;
  QPushButton *theme_ = nullptr;
  QPushButton *refresh_ = nullptr;
  QPushButton *minimize_ = nullptr;
  QPushButton *maximize_ = nullptr;
  QPushButton *close_ = nullptr;
  QLabel *status_ = nullptr;
  QLabel *ohlc_ = nullptr;
  QLabel *range_ = nullptr;
  QLabel *events_ = nullptr;
  QDialog *settingsDialog_ = nullptr;
  QDialog *indicatorDialog_ = nullptr;
  QDialog *backendDialog_ = nullptr;
  QDialog *debugDialog_ = nullptr;
  QPlainTextEdit *debugLog_ = nullptr;
  QPlainTextEdit *indicatorErrorText_ = nullptr;
  QWidget *customIndicatorList_ = nullptr;
  QVBoxLayout *customIndicatorLayout_ = nullptr;
  QLineEdit *backendUrl_ = nullptr;
  QLineEdit *wsUrl_ = nullptr;
  QCheckBox *realtime_ = nullptr;
  QCheckBox *fvgCircleEnabled_ = nullptr;
  QSpinBox *fvgCircleN_ = nullptr;
  QSpinBox *fvgCircleMinGapTicks_ = nullptr;
  QNetworkAccessManager updateNetwork_;
  CandleClient client_;
  QVector<Candle> loadedCandles_;
  QTimer replayTimer_;
  QByteArray timeZoneId_ = QTimeZone::systemTimeZoneId();
  qint64 lastRangeStartMs_ = 0;
  qint64 lastRangeEndMs_ = 0;
  qint64 replayLoadCursorMs_ = 0;
  int lastRangeFirstIndex_ = -1;
  int lastRangeLastIndex_ = -1;
  bool dark_ = true;
  bool replayActive_ = false;
  bool replayTimeTouched_ = false;
  bool replayStepping_ = false;
  bool windowDragging_ = false;
  bool resizingWindow_ = false;
  Qt::Edges resizeEdges_;
  QPoint resizeStartPos_;
  QRect resizeStartGeometry_;
  QPoint windowDragStart_;
  QRect normalGeometry_;
  bool maximizedAnimated_ = false;
  QPropertyAnimation *windowAnimation_ = nullptr;
};

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  loadBundledFonts();
  const QString appFontFamily = systemUiFontFamily();
  QFont appFont(appFontFamily);
  appFont.setPointSize(10);
  appFont.setWeight(QFont::Normal);
  appFont.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::PreferQuality));
  app.setFont(appFont);
  MainWindow window;
  window.show();
  return app.exec();
}

#include "main.moc"
