#include <QtWidgets>
#include <QtNetwork>
#include <QtWebSockets/QWebSocket>
#include <algorithm>
#include <cmath>
#include <limits>

struct Candle {
  qint64 ms = 0;
  double open = 0;
  double high = 0;
  double low = 0;
  double close = 0;
};

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

class ChartWidget : public QWidget {
  Q_OBJECT

public:
  explicit ChartWidget(QWidget *parent = nullptr) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
  }

  void setDark(bool value) {
    dark_ = value;
    update();
  }

  void setCandles(QVector<Candle> candles) {
    candles_ = std::move(candles);
    std::sort(candles_.begin(), candles_.end(), [](const Candle &a, const Candle &b) {
      return a.ms < b.ms;
    });
    if (visibleCount_ <= 0) visibleCount_ = std::min(160, candleCount());
    visibleStart_ = maxVisibleStart();
    hoveredIndex_ = -1;
    update();
  }

  void upsertCandle(const Candle &candle) {
    const auto it = std::lower_bound(candles_.begin(), candles_.end(), candle.ms, [](const Candle &item, qint64 ms) {
      return item.ms < ms;
    });
    const bool wasAtEnd = visibleStart_ + visibleCount_ >= candleCount() - 2;
    if (it != candles_.end() && it->ms == candle.ms) {
      *it = candle;
    } else {
      candles_.insert(it, candle);
    }
    if (wasAtEnd) visibleStart_ = maxVisibleStart();
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

signals:
  void hoveredCandleChanged(const Candle *candle);

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    paintBackground(p);
    paintGrid(p);
    paintCandles(p);
    paintLayerHints(p);
    paintCrosshair(p);
    paintOhlcSketch(p);
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    if (dragging_) {
      const int dx = event->position().x() - dragStart_.x();
      const int deltaBars = static_cast<int>(std::round(-dx / std::max(1.0, barStep())));
      visibleStart_ = std::clamp(dragVisibleStart_ + deltaBars, 0, maxVisibleStart());
      update();
      return;
    }
    hoveredIndex_ = indexAt(event->position().x());
    if (hoveredIndex_ >= 0 && hoveredIndex_ < candleCount()) {
      emit hoveredCandleChanged(&candles_[hoveredIndex_]);
    } else {
      emit hoveredCandleChanged(nullptr);
    }
    update();
  }

  void leaveEvent(QEvent *) override {
    hoveredIndex_ = -1;
    emit hoveredCandleChanged(nullptr);
    update();
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() != Qt::LeftButton) return;
    if (toggleLayerAt(event->position())) {
      update();
      return;
    }
    dragging_ = true;
    dragStart_ = event->position().toPoint();
    dragVisibleStart_ = visibleStart_;
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) dragging_ = false;
  }

  void wheelEvent(QWheelEvent *event) override {
    if (candles_.isEmpty()) return;
    const int before = visibleCount_;
    visibleCount_ += event->angleDelta().y() > 0 ? -16 : 16;
    visibleCount_ = std::clamp(visibleCount_, 40, std::max(40, candleCount()));
    const int mouseIndex = indexAt(event->position().x());
    if (mouseIndex >= 0) {
      const double ratio = (event->position().x() - plotRect().left()) / std::max(1.0, plotRect().width());
      visibleStart_ = mouseIndex - static_cast<int>(ratio * visibleCount_);
    } else if (before != visibleCount_) {
      visibleStart_ += (before - visibleCount_) / 2;
    }
    visibleStart_ = std::clamp(visibleStart_, 0, maxVisibleStart());
    update();
  }

private:
  QRectF plotRect() const {
    return rect().adjusted(16, 16, -74, -28);
  }

  double barStep() const {
    return plotRect().width() / std::max(1, visibleCount_);
  }

  int indexAt(double x) const {
    if (candles_.isEmpty() || !plotRect().contains(QPointF(x, plotRect().center().y()))) return -1;
    const int local = static_cast<int>((x - plotRect().left()) / std::max(1.0, barStep()));
    const int index = visibleStart_ + local;
    return index >= 0 && index < candleCount() ? index : -1;
  }

  int candleCount() const {
    return static_cast<int>(candles_.size());
  }

  int maxVisibleStart() const {
    return std::max(0, candleCount() - visibleCount_);
  }

  QColor bg() const { return dark_ ? QColor("#0b100f") : QColor("#fffefa"); }
  QColor text() const { return dark_ ? QColor("#f4efe3") : QColor("#131916"); }
  QColor muted() const { return dark_ ? QColor("#a7b0a8") : QColor("#59635d"); }
  QColor grid() const { return dark_ ? QColor(230, 226, 211, 20) : QColor(23, 31, 27, 22); }
  QColor up() const { return QColor("#20c997"); }
  QColor down() const { return QColor("#ef5f78"); }

  void visibleRange(double &minPrice, double &maxPrice) const {
    minPrice = std::numeric_limits<double>::max();
    maxPrice = std::numeric_limits<double>::lowest();
    const int end = std::min(candleCount(), visibleStart_ + visibleCount_);
    for (int i = visibleStart_; i < end; ++i) {
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
  }

  double yFor(double price, double minPrice, double maxPrice) const {
    const QRectF r = plotRect();
    return r.bottom() - ((price - minPrice) / (maxPrice - minPrice)) * r.height();
  }

  void paintBackground(QPainter &p) {
    p.fillRect(rect(), bg());
  }

  void paintGrid(QPainter &p) {
    const QRectF r = plotRect();
    p.setPen(QPen(grid(), 1));
    for (int i = 0; i <= 5; ++i) {
      const double y = r.top() + r.height() * i / 5.0;
      p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }
    for (int i = 0; i <= 8; ++i) {
      const double x = r.left() + r.width() * i / 8.0;
      p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }
  }

  void paintCandles(QPainter &p) {
    if (candles_.isEmpty()) return;
    double minPrice, maxPrice;
    visibleRange(minPrice, maxPrice);
    const QRectF r = plotRect();
    const double step = barStep();
    const double bodyWidth = std::clamp(step * 0.58, 2.0, 10.0);
    const int end = std::min(candleCount(), visibleStart_ + visibleCount_);
    for (int i = visibleStart_; i < end; ++i) {
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
  }

  void paintLayerHints(QPainter &p) {
    QFont f = font();
    f.setPixelSize(11);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    int y = 24;
    auto row = [&](bool visible, const QString &name) {
      p.setPen(visible ? text() : QColor(muted().red(), muted().green(), muted().blue(), 115));
      p.drawText(14, y, visible ? "◉" : "◌");
      p.drawText(34, y, name);
      y += 18;
    };
    row(rangeVisible_, "Range");
    row(nVisible_, "N");
    row(ninVisible_, "N-IN");
    row(ifvgVisible_, "iFVG");
    row(orderVisible_, "Order");
    row(markerVisible_, "Marker");
  }

  bool toggleLayerAt(const QPointF &point) {
    if (point.x() < 12 || point.x() > 170 || point.y() < 12 || point.y() > 136) return false;
    const int row = static_cast<int>((point.y() - 16) / 18);
    switch (row) {
      case 0: rangeVisible_ = !rangeVisible_; return true;
      case 1: nVisible_ = !nVisible_; return true;
      case 2: ninVisible_ = !ninVisible_; return true;
      case 3: ifvgVisible_ = !ifvgVisible_; return true;
      case 4: orderVisible_ = !orderVisible_; return true;
      case 5: markerVisible_ = !markerVisible_; return true;
      default: return false;
    }
  }

  void paintCrosshair(QPainter &p) {
    if (hoveredIndex_ < 0 || hoveredIndex_ >= candleCount()) return;
    const QRectF r = plotRect();
    const double x = r.left() + (hoveredIndex_ - visibleStart_ + 0.5) * barStep();
    p.setPen(QPen(dark_ ? QColor(244, 239, 227, 70) : QColor(19, 25, 22, 70), 1, Qt::DashLine));
    p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
  }

  void paintOhlcSketch(QPainter &p) {
    if (hoveredIndex_ < 0 || hoveredIndex_ >= candleCount()) return;
    const Candle &c = candles_[hoveredIndex_];
    const bool green = c.close >= c.open;
    const QColor color = green ? up() : down();
    QRectF panel(width() - 224, 18, 142, 150);
    p.setPen(QPen(dark_ ? QColor(230, 226, 211, 34) : QColor(23, 31, 27, 34), 1));
    p.setBrush(dark_ ? QColor(14, 19, 17, 178) : QColor(255, 253, 247, 210));
    p.drawRect(panel);

    QFont label = font();
    label.setPixelSize(9);
    label.setWeight(QFont::Black);
    p.setFont(label);
    p.setPen(QColor("#f0b64f"));
    p.drawText(panel.adjusted(8, 8, 0, 0), "OHLC");

    const double top = panel.top() + 30;
    const double bodyTop = panel.top() + 58;
    const double bodyBottom = panel.top() + 96;
    const double bottom = panel.bottom() - 16;
    const double cx = panel.right() - 52;
    p.setPen(QPen(color, 2));
    p.drawLine(QPointF(cx, top), QPointF(cx, bottom));
    QRectF body(cx - 11, bodyTop, 22, bodyBottom - bodyTop);
    p.fillRect(body, color);
    p.drawRect(body);

    QFont nums = font();
    nums.setPixelSize(10);
    nums.setWeight(QFont::DemiBold);
    p.setFont(nums);
    p.setPen(muted());
    p.drawText(QPointF(panel.left() + 8, green ? bodyBottom + 3 : bodyTop + 3), QString("O %1").arg(c.open));
    p.setPen(color);
    p.drawText(QPointF(panel.left() + 8, green ? bodyTop + 3 : bodyBottom + 3), QString("C %1").arg(c.close));
    p.setPen(QColor("#f0b64f"));
    p.drawText(QPointF(panel.right() - 44, top + 3), QString("H %1").arg(c.high));
    p.setPen(QColor("#72d9f7"));
    p.drawText(QPointF(panel.right() - 44, bottom + 3), QString("L %1").arg(c.low));
  }

  QVector<Candle> candles_;
  bool dark_ = true;
  bool rangeVisible_ = true;
  bool nVisible_ = true;
  bool ninVisible_ = true;
  bool ifvgVisible_ = true;
  bool orderVisible_ = true;
  bool markerVisible_ = true;
  int visibleStart_ = 0;
  int visibleCount_ = 160;
  int hoveredIndex_ = -1;
  bool dragging_ = false;
  QPoint dragStart_;
  int dragVisibleStart_ = 0;
};

class CandleClient : public QObject {
  Q_OBJECT

public:
  explicit CandleClient(QObject *parent = nullptr) : QObject(parent) {
    backendBase_ = normalizeBase(envOrDefault("Q4J_BACKEND_URL", "http://127.0.0.1:8080"));
    wsBase_ = normalizeBase(envOrDefault("Q4J_WS_BASE", wsFromHttp(backendBase_)));
    connect(&socket_, &QWebSocket::connected, this, [this] { emit statusChanged("实时", true); });
    connect(&socket_, &QWebSocket::disconnected, this, [this] { emit statusChanged("断开", false); });
    connect(&socket_, &QWebSocket::textMessageReceived, this, &CandleClient::onSocketMessage);
    connect(&socket_, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, [this] {
      emit statusChanged("断开", false);
    });
  }

  QString backendBase() const { return backendBase_; }
  QString wsBase() const { return wsBase_; }

  void configureBackend(const QString &backendBase, const QString &wsBase) {
    backendBase_ = normalizeBase(backendBase.trimmed());
    wsBase_ = normalizeBase(wsBase.trimmed().isEmpty() ? wsFromHttp(backendBase_) : wsBase.trimmed());
    socket_.close();
  }

  void load(const QString &symbol, const QString &interval) {
    symbol_ = symbol.trimmed();
    interval_ = interval.trimmed();
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
      reply->deleteLater();
      if (reply->error() != QNetworkReply::NoError) {
        emit statusChanged("加载失败", false);
        return;
      }
      const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
      QVector<Candle> candles;
      for (const QJsonValue &value : doc.array()) {
        const QJsonObject obj = value.toObject();
        candles.push_back(parseCandle(obj));
      }
      emit candlesLoaded(candles);
      connectSocket();
    });
  }

signals:
  void candlesLoaded(const QVector<Candle> &candles);
  void candleUpdated(const Candle &candle);
  void statusChanged(const QString &status, bool live);

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

  void connectSocket() {
    socket_.close();
    QUrl url(wsBase_ + "/ws/candles");
    QUrlQuery query;
    query.addQueryItem("symbol", symbol_);
    query.addQueryItem("interval", interval_);
    url.setQuery(query);
    socket_.open(url);
  }

  void onSocketMessage(const QString &message) {
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) return;
    emit candleUpdated(parseCandle(doc.object()));
  }

  QString backendBase_;
  QString wsBase_;
  QString symbol_;
  QString interval_;
  QNetworkAccessManager network_;
  QWebSocket socket_;
};

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow() {
    setWindowTitle("Q4J Market Structure Desk");
    resize(1440, 860);
    buildUi();
    bindSignals();
    applyTheme();
    if (showBackendDialog(true)) {
      refresh();
    } else {
      status_->setText("○ 后端未配置");
    }
  }

private:
  void buildUi() {
    auto *root = new QWidget;
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    setCentralWidget(root);

    auto *header = new QFrame;
    header->setObjectName("header");
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(9, 7, 9, 7);
    headerLayout->setSpacing(12);

    auto *brand = new QLabel("<b>Q4J</b>&nbsp;&nbsp;Market Structure Desk<br><span style='font-size:18px'>Execution Map</span>");
    headerLayout->addWidget(brand);
    headerLayout->addSpacing(16);

    symbol_ = new QLineEdit("JP225");
    interval_ = new QComboBox;
    interval_->addItems({"1m", "2m", "3m", "5m", "10m", "15m", "30m", "1h", "4h", "1d"});
    settings_ = new QPushButton("策略设置");
    backend_ = new QPushButton("后端");
    theme_ = new QPushButton("☾");
    refresh_ = new QPushButton("刷新");
    status_ = new QLabel("连接中");
    status_->setObjectName("status");
    symbol_->setFixedWidth(150);
    interval_->setFixedWidth(76);
    settings_->setFixedWidth(76);
    backend_->setFixedWidth(54);
    theme_->setFixedWidth(42);
    refresh_->setFixedWidth(62);
    status_->setFixedWidth(86);
    headerLayout->addWidget(symbol_);
    headerLayout->addWidget(interval_);
    headerLayout->addWidget(settings_);
    headerLayout->addWidget(backend_);
    headerLayout->addStretch(1);
    headerLayout->addWidget(theme_);
    headerLayout->addWidget(refresh_);
    headerLayout->addWidget(status_);
    layout->addWidget(header);

    chart_ = new ChartWidget;
    layout->addWidget(chart_, 1);

    auto *footer = new QFrame;
    footer->setObjectName("footer");
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(8, 6, 8, 6);
    footerLayout->setSpacing(16);
    ohlc_ = new QLabel("O -- H -- L -- C --");
    range_ = new QLabel("Visible Range --");
    events_ = new QLabel("Events --");
    footerLayout->addWidget(ohlc_, 2);
    footerLayout->addWidget(range_, 3);
    footerLayout->addWidget(events_, 1);
    layout->addWidget(footer);

    buildSettingsDialog();
    buildBackendDialog();
  }

  void buildSettingsDialog() {
    settingsDialog_ = new QDialog(this);
    settingsDialog_->setWindowTitle("策略设置");
    auto *layout = new QFormLayout(settingsDialog_);
    higher_ = new QComboBox;
    lower_ = new QComboBox;
    higher_->addItems({"15m", "30m", "1h", "4h"});
    lower_->addItems({"1m", "2m", "3m", "5m", "10m", "15m"});
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    layout->addRow("高周期", higher_);
    layout->addRow("低周期", lower_);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, settingsDialog_, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
      settingsDialog_->accept();
      refresh();
    });
  }

  void buildBackendDialog() {
    backendDialog_ = new QDialog(this);
    backendDialog_->setWindowTitle("后端接口");
    auto *layout = new QVBoxLayout(backendDialog_);
    auto *form = new QFormLayout;
    backendUrl_ = new QLineEdit(client_.backendBase());
    wsUrl_ = new QLineEdit(client_.wsBase());
    backendUrl_->setPlaceholderText("http://127.0.0.1:8080");
    wsUrl_->setPlaceholderText("留空则从 HTTP 地址自动推导");
    form->addRow("HTTP 后端", backendUrl_);
    form->addRow("WebSocket", wsUrl_);
    layout->addLayout(form);

    auto *hint = new QLabel("HTTP 地址用于 /api/candles，WebSocket 地址用于 /ws/candles。");
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
    while (true) {
      if (backendDialog_->exec() != QDialog::Accepted) {
        return false;
      }
      const QString backend = backendUrl_->text().trimmed();
      if (backend.isEmpty()) {
        QMessageBox::warning(this, "后端接口", "HTTP 后端地址不能为空。");
        continue;
      }
      client_.configureBackend(backend, wsUrl_->text());
      if (!startup) refresh();
      return true;
    }
  }

  void bindSignals() {
    connect(refresh_, &QPushButton::clicked, this, &MainWindow::refresh);
    connect(backend_, &QPushButton::clicked, this, [this] {
      showBackendDialog(false);
    });
    connect(theme_, &QPushButton::clicked, this, [this] {
      dark_ = !dark_;
      applyTheme();
    });
    connect(settings_, &QPushButton::clicked, settingsDialog_, &QDialog::show);
    connect(&client_, &CandleClient::candlesLoaded, chart_, &ChartWidget::setCandles);
    connect(&client_, &CandleClient::candleUpdated, chart_, &ChartWidget::upsertCandle);
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
    const QString css = dark_ ? darkCss() : lightCss();
    qApp->setStyleSheet(css);
  }

  QString darkCss() const {
    return R"(
      QWidget { background: #080c0b; color: #f4efe3; font-family: "SF Pro Text", "Segoe UI", "PingFang SC"; font-size: 12px; }
      QFrame#header, QFrame#footer { background: rgba(14, 19, 17, 220); border: 1px solid rgba(230, 226, 211, 32); border-radius: 3px; }
      QLineEdit, QComboBox, QPushButton { height: 30px; background: rgba(31, 39, 35, 190); border: 1px solid rgba(230, 226, 211, 62); border-radius: 2px; padding: 0 8px; }
      QPushButton:hover { border-color: #f0b64f; }
      QLabel#status { color: #a7b0a8; }
    )";
  }

  QString lightCss() const {
    return R"(
      QWidget { background: #eef0eb; color: #131916; font-family: "SF Pro Text", "Segoe UI", "PingFang SC"; font-size: 12px; }
      QFrame#header, QFrame#footer { background: rgba(255, 253, 247, 235); border: 1px solid rgba(23, 31, 27, 32); border-radius: 3px; }
      QLineEdit, QComboBox, QPushButton { height: 30px; background: rgba(238, 241, 234, 210); border: 1px solid rgba(23, 31, 27, 62); border-radius: 2px; padding: 0 8px; }
      QPushButton:hover { border-color: #f0b64f; }
      QLabel#status { color: #59635d; }
    )";
  }

  void refresh() {
    client_.load(symbol_->text(), interval_->currentText());
  }

  ChartWidget *chart_ = nullptr;
  QLineEdit *symbol_ = nullptr;
  QComboBox *interval_ = nullptr;
  QComboBox *higher_ = nullptr;
  QComboBox *lower_ = nullptr;
  QPushButton *settings_ = nullptr;
  QPushButton *backend_ = nullptr;
  QPushButton *theme_ = nullptr;
  QPushButton *refresh_ = nullptr;
  QLabel *status_ = nullptr;
  QLabel *ohlc_ = nullptr;
  QLabel *range_ = nullptr;
  QLabel *events_ = nullptr;
  QDialog *settingsDialog_ = nullptr;
  QDialog *backendDialog_ = nullptr;
  QLineEdit *backendUrl_ = nullptr;
  QLineEdit *wsUrl_ = nullptr;
  CandleClient client_;
  bool dark_ = true;
};

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  MainWindow window;
  window.show();
  return app.exec();
}

#include "main.moc"
