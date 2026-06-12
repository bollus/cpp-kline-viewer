#pragma once

#include <QObject>
#include <QTimer>
#include <QSettings>
#include <QTimeZone>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDesktopServices>
#include <QUrl>
#include <QVersionNumber>
#include <QRegularExpression>
#include <algorithm>

#include "candle_client.hpp"
#include "chart_item.hpp"
#include "models.hpp"
#include "theme.hpp"

#ifndef Q4J_APP_VERSION
#define Q4J_APP_VERSION "1.0.0"
#endif
#ifndef Q4J_UPDATE_REPO
#define Q4J_UPDATE_REPO ""
#endif

// Owns all non-UI application logic that used to live in MainWindow: backend
// configuration, candle/strategy loading, the replay engine, persistence,
// theme/timezone state and the log feed. Exposed to QML via properties,
// invokables and signals.
class AppController : public QObject {
  Q_OBJECT
  Q_PROPERTY(ChartItem *chart READ chart WRITE setChart NOTIFY chartChanged)
  Q_PROPERTY(LogModel *logModel READ logModel NOTIFY modelsChanged)
  Q_PROPERTY(StrategyModel *strategyModel READ strategyModel NOTIFY modelsChanged)

  Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY darkChanged)
  Q_PROPERTY(QString symbol READ symbol WRITE setSymbol NOTIFY symbolChanged)
  Q_PROPERTY(QString timeframe READ timeframe NOTIFY timeframeChanged)
  Q_PROPERTY(QString timeframeLabel READ timeframeLabelCurrent NOTIFY timeframeChanged)
  Q_PROPERTY(QString strategyName READ strategyName WRITE setStrategyName NOTIFY strategyNameChanged)

  Q_PROPERTY(bool backendConfigured READ backendConfigured NOTIFY backendChanged)
  Q_PROPERTY(QString backendBase READ backendBase NOTIFY backendChanged)
  Q_PROPERTY(QString wsBase READ wsBase NOTIFY backendChanged)
  Q_PROPERTY(bool realtimeEnabled READ realtimeEnabled NOTIFY backendChanged)

  Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY statusChanged)
  Q_PROPERTY(bool connectionLive READ connectionLive NOTIFY statusChanged)

  Q_PROPERTY(QString marketPrice READ marketPrice NOTIFY marketChanged)
  Q_PROPERTY(QString marketChange READ marketChange NOTIFY marketChanged)
  Q_PROPERTY(bool marketUp READ marketUp NOTIFY marketChanged)
  Q_PROPERTY(QString ohlcText READ ohlcText NOTIFY ohlcChanged)
  Q_PROPERTY(int eventCount READ eventCount NOTIFY eventCountChanged)
  Q_PROPERTY(QString visibleRangeText READ visibleRangeText NOTIFY visibleRangeChanged)
  Q_PROPERTY(QString dataStartText READ dataStartText NOTIFY replayChanged)

  Q_PROPERTY(bool replayActive READ replayActive WRITE setReplayActive NOTIFY replayChanged)
  Q_PROPERTY(bool replayPlaying READ replayPlaying NOTIFY replayChanged)
  Q_PROPERTY(int replaySpeed READ replaySpeed WRITE setReplaySpeed NOTIFY replayChanged)
  Q_PROPERTY(double replayProgress READ replayProgress NOTIFY replayChanged)
  Q_PROPERTY(QString replayCursorText READ replayCursorText NOTIFY replayChanged)
  Q_PROPERTY(qint64 replayCursorMs READ replayCursorMs WRITE setReplayCursorMs NOTIFY replayChanged)

  Q_PROPERTY(QDateTime replayCursorDateTime READ replayCursorDateTime NOTIFY replayChanged)
  Q_PROPERTY(QDateTime replayMinDateTime READ replayMinDateTime NOTIFY replayChanged)
  Q_PROPERTY(QDateTime replayMaxDateTime READ replayMaxDateTime NOTIFY replayChanged)

  Q_PROPERTY(bool magnetEnabled READ magnetEnabled WRITE setMagnetEnabled NOTIFY magnetChanged)
  Q_PROPERTY(int annotationTool READ annotationTool WRITE setAnnotationTool NOTIFY annotationToolChanged)

  Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
  Q_PROPERTY(QString updateStatus READ updateStatus NOTIFY updateChanged)
  Q_PROPERTY(bool updateChecking READ updateChecking NOTIFY updateChanged)
  Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateChanged)
  Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY updateChanged)

public:
  explicit AppController(QObject *parent = nullptr) : QObject(parent) {
    logModel_ = new LogModel(this);
    strategyModel_ = new StrategyModel(this);
    replayTimer_.setInterval(replayIntervalForSpeed(replaySpeed_));
    connect(&replayTimer_, &QTimer::timeout, this, &AppController::stepReplay);
    wireClient();
  }

  // ---- Bootstrap (called once after construction) --------------------------
  Q_INVOKABLE void start() {
    loadBackendSettings();
    loadStrategySettings();
    loadDisplaySettings();
    if (client_.hasConfiguredBackend()) {
      client_.loadOverlayStrategies();
      refresh();
    } else {
      connectionStatus_ = "后端未配置";
      connectionLive_ = false;
      emit statusChanged();
      emit needsBackendConfig();
    }
    emit backendChanged();
  }

  ChartItem *chart() const { return chart_; }
  void setChart(ChartItem *chart) {
    if (chart_ == chart) return;
    chart_ = chart;
    if (chart_) {
      chart_->setDark(dark_);
      chart_->setTimeZoneId(timeZoneId_);
      connect(chart_, &ChartItem::olderCandlesRequested, &client_, &CandleClient::loadOlder);
      connect(chart_, &ChartItem::overlayRangeChanged, &client_, &CandleClient::loadOverlayRange);
      connect(chart_, &ChartItem::visibleRangeChanged, this,
              [this](qint64 startMs, qint64 endMs, int firstIndex, int lastIndex) {
                if (startMs <= 0 || endMs <= 0 || firstIndex < 0 || lastIndex < 0) {
                  visibleRangeText_ = "可视范围 --";
                } else {
                  visibleRangeText_ = QString("可视范围 %1 → %2")
                      .arg(formatTime(startMs, "MM-dd HH:mm"), formatTime(endMs, "MM-dd HH:mm"));
                }
                emit visibleRangeChanged();
              });
      // Only user-driven pan/zoom drives cross-view sync (programmatic loads do
      // not, so a freshly loaded pane never resets its siblings).
      connect(chart_, &ChartItem::viewportInteracted, this, [this](qint64 startMs, qint64 endMs) {
        emit viewportChanged(startMs, endMs);
      });
      connect(chart_, &ChartItem::crosshairMoved, this, [this](qint64 ms, bool active) {
        emit crosshairChanged(ms, active);
      });
      connect(chart_, &ChartItem::chartPressed, this, [this]() { emit activated(); });
      connect(chart_, &ChartItem::hoveredCandleChanged, this, [this](const Candle *c) {
        if (!c) {
          ohlcText_.clear();
        } else {
          ohlcText_ = QString("开 %1  高 %2  低 %3  收 %4")
              .arg(c->open).arg(c->high).arg(c->low).arg(c->close);
        }
        emit ohlcChanged();
      });
      connect(chart_, &ChartItem::annotationToolChanged, this, [this](ChartItem::AnnotationTool tool) {
        annotationTool_ = static_cast<int>(tool);
        emit annotationToolChanged();
      });
    }
    emit chartChanged();
  }

  LogModel *logModel() const { return logModel_; }
  StrategyModel *strategyModel() const { return strategyModel_; }

  // Replace the per-view models with workspace-shared instances so every pane
  // feeds one log panel / strategy list. Must be called before start/begin.
  void setSharedModels(LogModel *log, StrategyModel *strategy) {
    if (log && log != logModel_) {
      if (logModel_ && logModel_->parent() == this) logModel_->deleteLater();
      logModel_ = log;
    }
    if (strategy && strategy != strategyModel_) {
      if (strategyModel_ && strategyModel_->parent() == this) strategyModel_->deleteLater();
      strategyModel_ = strategy;
    }
    emit modelsChanged();
  }

  // ---- Simple state accessors ---------------------------------------------
  bool dark() const { return dark_; }
  void setDark(bool value) {
    if (dark_ == value) return;
    dark_ = value;
    if (chart_) chart_->setDark(dark_);
    emit darkChanged();
  }

  QString symbol() const { return symbol_; }
  void setSymbol(const QString &value) {
    if (symbol_ == value) return;
    symbol_ = value;
    emit symbolChanged();
  }

  QString timeframe() const { return timeframe_; }
  QString timeframeLabelCurrent() const { return timeframeLabel(timeframe_); }

  QString strategyName() const { return strategyName_; }
  void setStrategyName(const QString &value) {
    const QString resolved = value.trimmed().isEmpty() ? QString("n_in_range_variant") : value.trimmed();
    if (strategyName_ == resolved) return;
    strategyName_ = resolved;
    emit strategyNameChanged();
  }

  bool backendConfigured() const { return client_.hasConfiguredBackend(); }
  QString backendBase() const { return client_.backendBase(); }
  QString wsBase() const { return client_.wsBase(); }
  bool realtimeEnabled() const { return client_.realtimeEnabled(); }

  QString connectionStatus() const { return connectionStatus_; }
  bool connectionLive() const { return connectionLive_; }

  QString marketPrice() const { return marketPrice_; }
  QString marketChange() const { return marketChange_; }
  bool marketUp() const { return marketUp_; }
  QString ohlcText() const { return ohlcText_; }
  int eventCount() const { return eventCount_; }
  QString visibleRangeText() const { return visibleRangeText_; }
  QString dataStartText() const { return dataStartText_; }

  bool replayActive() const { return replayActive_; }
  bool replayPlaying() const { return replayTimer_.isActive(); }
  int replaySpeed() const { return replaySpeed_; }
  qint64 replayCursorMs() const { return replayCursorMs_; }

  double replayProgress() const {
    if (loaded_.isEmpty()) return 0.0;
    const qint64 first = loaded_.first().ms;
    const qint64 last = loaded_.last().ms;
    if (last <= first) return 0.0;
    return std::clamp(double(replayCursorMs_ - first) / double(last - first), 0.0, 1.0);
  }

  QString replayCursorText() const { return formatTime(replayCursorMs_, "yyyy-MM-dd HH:mm"); }

  QDateTime replayCursorDateTime() const { return msToZoned(replayCursorMs_ > 0 ? replayCursorMs_ : (loaded_.isEmpty() ? 0 : loaded_.first().ms)); }
  QDateTime replayMinDateTime() const { return msToZoned(loaded_.isEmpty() ? 0 : loaded_.first().ms); }
  QDateTime replayMaxDateTime() const { return msToZoned(loaded_.isEmpty() ? 0 : loaded_.last().ms); }

  QString appVersion() const { return QStringLiteral(Q4J_APP_VERSION); }
  QString updateStatus() const { return updateStatus_; }
  bool updateChecking() const { return updateChecking_; }
  bool updateAvailable() const { return updateAvailable_; }
  QString latestVersion() const { return latestVersion_; }

  bool magnetEnabled() const { return magnetEnabled_; }
  void setMagnetEnabled(bool value) {
    if (magnetEnabled_ == value) return;
    magnetEnabled_ = value;
    if (chart_) chart_->setMagnetEnabled(value);
    emit magnetChanged();
  }

  int annotationTool() const { return annotationTool_; }
  void setAnnotationTool(int tool) {
    annotationTool_ = tool;
    if (chart_) chart_->setAnnotationTool(static_cast<ChartItem::AnnotationTool>(tool));
    emit annotationToolChanged();
  }

  // ---- Commands ------------------------------------------------------------
  Q_INVOKABLE void setTimeframe(const QString &token) {
    timeframe_ = token;
    emit timeframeChanged();
    QTimer::singleShot(0, this, &AppController::refresh);
  }

  // Set timeframe without triggering a reload (the workspace configures the
  // backend afterwards which performs the single refresh).
  Q_INVOKABLE void setTimeframeQuiet(const QString &token) {
    if (timeframe_ == token) return;
    timeframe_ = token;
    emit timeframeChanged();
  }

  Q_INVOKABLE void refresh() {
    if (!client_.hasConfiguredBackend()) {
      emit needsBackendConfig();
      return;
    }
    visibleRangeText_ = "可视范围 --";
    eventCount_ = 0;
    loaded_.clear();
    replayLoadCursorMs_ = 0;
    replayTimer_.stop();
    emit visibleRangeChanged();
    emit eventCountChanged();
    emit replayChanged();
    if (chart_) {
      chart_->clearMessage();
      chart_->setCandles({});
      chart_->setOverlayEvents(QJsonArray{});
    }
    client_.load(symbol_, timeframe_, strategyName_);
  }

  Q_INVOKABLE void loadStrategies() {
    if (client_.hasConfiguredBackend()) client_.loadOverlayStrategies();
  }

  Q_INVOKABLE void loadStrategy(const QString &name) {
    if (!name.trimmed().isEmpty()) setStrategyName(name);
    saveStrategySettings();
    refresh();
  }

  Q_INVOKABLE void configureBackend(const QString &http, const QString &ws, bool realtime) {
    const QString trimmed = http.trimmed();
    if (trimmed.isEmpty()) return;
    client_.configureBackend(trimmed, ws.trimmed(), realtime);
    saveBackendSettings();
    emit backendChanged();
    client_.loadOverlayStrategies();
    refresh();
  }

  // Configure this pane's client without persisting or reloading the strategy
  // list (the workspace handles persistence + a single strategy fetch).
  Q_INVOKABLE void configureBackendSilent(const QString &http, const QString &ws, bool realtime) {
    const QString trimmed = http.trimmed();
    if (trimmed.isEmpty()) return;
    client_.configureBackend(trimmed, ws.trimmed(), realtime);
    emit backendChanged();
    refresh();
  }

  Q_INVOKABLE void loadOverlayStrategiesOnce() {
    if (client_.hasConfiguredBackend()) client_.loadOverlayStrategies();
  }

  // ---- Cross-view synchronisation (driven by Workspace) --------------------
  Q_INVOKABLE void applySyncedRange(qint64 startMs, qint64 endMs) {
    if (chart_) chart_->setVisibleRangeByTime(startMs, endMs);
  }
  Q_INVOKABLE void applySyncedCrosshair(qint64 ms, bool active) {
    if (chart_) chart_->setSyncCrosshairTime(ms, active);
  }
  qint64 viewportStartMs() const { return chart_ ? chart_->visibleStartMs() : 0; }
  qint64 viewportEndMs() const { return chart_ ? chart_->visibleEndMs() : 0; }

  // ---- Replay --------------------------------------------------------------
  void setReplayActive(bool active) {
    if (replayActive_ == active) return;
    replayActive_ = active;
    if (active && replayCursorMs_ == 0 && !loaded_.isEmpty()) replayCursorMs_ = loaded_.first().ms;
    if (!active) replayTimer_.stop();
    applyReplayView();
    emit replayChanged();
  }

  void setReplaySpeed(int speed) {
    replaySpeed_ = std::max(1, speed);
    replayTimer_.setInterval(replayIntervalForSpeed(replaySpeed_));
    emit replayChanged();
  }

  void setReplayCursorMs(qint64 ms) {
    if (replayCursorMs_ == ms) return;
    replayCursorMs_ = ms;
    if (replayActive_) applyReplayView();
    emit replayChanged();
  }

  Q_INVOKABLE void replayPlayPause() {
    if (!replayActive_) setReplayActive(true);
    if (replayTimer_.isActive()) replayTimer_.stop();
    else replayTimer_.start();
    emit replayChanged();
  }

  Q_INVOKABLE void replayStepForward() { ensureReplay(); stepReplay(); }
  Q_INVOKABLE void replayStepBackward() {
    ensureReplay();
    if (loaded_.isEmpty()) return;
    auto it = std::lower_bound(loaded_.begin(), loaded_.end(), replayCursorMs_, [](const Candle &c, qint64 ms) { return c.ms < ms; });
    if (it == loaded_.begin()) return;
    --it;
    if (it->ms >= replayCursorMs_ && it != loaded_.begin()) --it;
    replayCursorMs_ = it->ms;
    applyReplayView();
    emit replayChanged();
  }

  Q_INVOKABLE void replayJumpToStart() {
    ensureReplay();
    if (loaded_.isEmpty()) return;
    replayCursorMs_ = loaded_.first().ms;
    applyReplayView();
    emit replayChanged();
  }

  Q_INVOKABLE void replayJumpToEnd() {
    ensureReplay();
    if (loaded_.isEmpty()) return;
    replayCursorMs_ = loaded_.last().ms;
    applyReplayView();
    emit replayChanged();
  }

  Q_INVOKABLE void replaySeek(double progress) {
    ensureReplay();
    if (loaded_.isEmpty()) return;
    const qint64 first = loaded_.first().ms;
    const qint64 last = loaded_.last().ms;
    replayCursorMs_ = first + static_cast<qint64>(double(last - first) * std::clamp(progress, 0.0, 1.0));
    applyReplayView();
    emit replayChanged();
  }

  // Jump the replay cursor to a wall-clock datetime (from the QML date picker).
  Q_INVOKABLE void setReplayCursorDateTime(const QDateTime &dt) {
    if (!dt.isValid()) return;
    QTimeZone zone(timeZoneId_);
    if (!zone.isValid()) zone = QTimeZone::systemTimeZone();
    QDateTime zoned = dt;
    zoned.setTimeZone(zone);
    qint64 ms = zoned.toMSecsSinceEpoch();
    if (!loaded_.isEmpty()) ms = std::clamp(ms, loaded_.first().ms, loaded_.last().ms);
    ensureReplay();
    setReplayCursorMs(ms);
  }

  // Parse a "yyyy-MM-dd HH:mm" string typed by the user.
  Q_INVOKABLE void setReplayCursorText(const QString &text) {
    QDateTime dt = QDateTime::fromString(text.trimmed(), "yyyy-MM-dd HH:mm");
    if (!dt.isValid()) dt = QDateTime::fromString(text.trimmed(), "yyyy-MM-dd HH:mm:ss");
    setReplayCursorDateTime(dt);
  }

  // ---- Update check --------------------------------------------------------
  Q_INVOKABLE void checkForUpdates() {
    const QString repo = QStringLiteral(Q4J_UPDATE_REPO);
    if (repo.isEmpty()) {
      updateStatus_ = "未配置更新源";
      updateChecking_ = false;
      updateAvailable_ = false;
      emit updateChanged();
      return;
    }
    if (updateChecking_) return;
    updateChecking_ = true;
    updateAvailable_ = false;
    updateStatus_ = "正在检查更新...";
    emit updateChanged();

    QNetworkRequest req(QUrl(QString("https://api.github.com/repos/%1/releases/latest").arg(repo)));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "AlgoHub-KLineViewer");
    QNetworkReply *reply = net_.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
      reply->deleteLater();
      updateChecking_ = false;
      if (reply->error() != QNetworkReply::NoError) {
        updateStatus_ = "检查更新失败：" + reply->errorString();
        emit updateChanged();
        return;
      }
      const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
      const QString tag = obj.value("tag_name").toString();
      downloadUrl_ = obj.value("html_url").toString();
      latestVersion_ = tag;
      const QVersionNumber latest = QVersionNumber::fromString(QString(tag).remove(QRegularExpression("^[vV]")));
      const QVersionNumber current = QVersionNumber::fromString(appVersion());
      if (!latest.isNull() && latest > current) {
        updateAvailable_ = true;
        updateStatus_ = QString("发现新版本 %1").arg(tag);
      } else {
        updateAvailable_ = false;
        updateStatus_ = QString("已是最新版本 (%1)").arg(appVersion());
      }
      emit updateChanged();
    });
  }

  Q_INVOKABLE void openDownloadPage() {
    const QString url = downloadUrl_.isEmpty()
        ? QString("https://github.com/%1/releases/latest").arg(QStringLiteral(Q4J_UPDATE_REPO))
        : downloadUrl_;
    QDesktopServices::openUrl(QUrl(url));
  }

signals:
  void chartChanged();
  void darkChanged();
  void symbolChanged();
  void timeframeChanged();
  void strategyNameChanged();
  void backendChanged();
  void statusChanged();
  void marketChanged();
  void ohlcChanged();
  void eventCountChanged();
  void visibleRangeChanged();
  void replayChanged();
  void magnetChanged();
  void annotationToolChanged();
  void needsBackendConfig();
  void strategiesLoaded();
  void updateChanged();
  void modelsChanged();
  void viewportChanged(qint64 startMs, qint64 endMs);
  void crosshairChanged(qint64 timeMs, bool active);
  void dataLoaded();
  void activated();

private:
  QDateTime msToZoned(qint64 ms) const {
    QTimeZone zone(timeZoneId_);
    if (!zone.isValid()) zone = QTimeZone::systemTimeZone();
    if (ms <= 0) return QDateTime::currentDateTime();
    return QDateTime::fromMSecsSinceEpoch(ms, zone);
  }

  void wireClient() {
    connect(&client_, &CandleClient::candlesLoaded, this, [this](const QVector<Candle> &candles) {
      loaded_ = candles;
      normalizeLoaded();
      if (loaded_.isEmpty()) {
        if (chart_) chart_->showLoadError("没有K线数据");
        return;
      }
      syncReplayBounds();
      applyReplayView();
      updateMarketSummary();
      emit dataLoaded();
    });
    connect(&client_, &CandleClient::olderCandlesLoaded, this, [this](const QVector<Candle> &candles) {
      loaded_ += candles;
      normalizeLoaded();
      syncReplayBounds();
      applyReplayView();
    });
    connect(&client_, &CandleClient::replayCandlesLoaded, this, [this](const QVector<Candle> &candles) {
      loaded_ += candles;
      normalizeLoaded();
      replayLoadCursorMs_ = 0;
      applyReplayView();
    });
    connect(&client_, &CandleClient::overlayEventsLoaded, this, [this](const QJsonValue &events) {
      if (chart_) chart_->setOverlayEvents(events);
      int count = 0;
      if (events.isArray()) count = events.toArray().size();
      else if (events.isObject()) count = events.toObject().value("layers").toArray().size();
      eventCount_ = count;
      emit eventCountChanged();
    });
    connect(&client_, &CandleClient::overlayStrategiesLoaded, this, [this](const QStringList &strategies) {
      QStringList values = strategies;
      if (!values.contains("n_in_range_variant", Qt::CaseInsensitive)) values.prepend("n_in_range_variant");
      values.removeDuplicates();
      values.sort(Qt::CaseInsensitive);
      strategyModel_->setStrategies(values);
      if (strategyModel_->indexOf(strategyName_) < 0 && !values.isEmpty()) setStrategyName(values.first());
      emit strategiesLoaded();
    });
    connect(&client_, &CandleClient::candleUpdated, this, [this](const Candle &candle) {
      logModel_->append("DEBUG", "Chart", QString("upsert candle %1 C=%2")
        .arg(QDateTime::fromMSecsSinceEpoch(candle.ms).toString("HH:mm:ss")).arg(candle.close));
      upsertLoadedCandle(candle);
      if (!replayActive_ && chart_) chart_->upsertCandle(candle);
      updateMarketSummary();
    });
    connect(&client_, &CandleClient::debugLog, this, [this](const QString &message) {
      logModel_->append(classifyLogLevel(message), deriveLogModule(message), message);
    });
    connect(&client_, &CandleClient::errorMessage, this, [this](const QString &message) {
      if (!chart_) return;
      if (message.trimmed().isEmpty()) chart_->clearMessage();
      else chart_->showMessage(message.trimmed());
    });
    connect(&client_, &CandleClient::loadFailed, this, [this](const QString &message) {
      if (chart_) chart_->showLoadError(message.trimmed().isEmpty() ? "加载失败" : message.trimmed());
      visibleRangeText_ = "可视范围 --";
      emit visibleRangeChanged();
    });
    connect(&client_, &CandleClient::statusChanged, this, [this](const QString &status, bool live) {
      connectionStatus_ = status;
      connectionLive_ = live;
      emit statusChanged();
      emit backendChanged();
    });
  }

  void ensureReplay() { if (!replayActive_) setReplayActive(true); }

  QVector<Candle> replayCandles() const {
    if (!replayActive_) return loaded_;
    QVector<Candle> visible;
    visible.reserve(loaded_.size());
    for (const Candle &c : loaded_) {
      if (c.ms <= replayCursorMs_) visible.push_back(c);
      else break;
    }
    return visible;
  }

  bool hasReplayDataForCursor(qint64 cursor) const {
    if (!replayActive_) return true;
    if (loaded_.isEmpty()) return false;
    return cursor >= loaded_.first().ms;
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
    if (replayActive_ && requestReplayDataIfNeeded(replayCursorMs_)) return;
    chart_->setReplayCandles(replayCandles());
    if (replayActive_ && !loaded_.isEmpty()) chart_->setReplayMarker(loaded_.first().ms, true);
    else chart_->setReplayMarker(0, false);
    if (!loaded_.isEmpty())
      dataStartText_ = "数据起点：" + formatTime(loaded_.first().ms, "yyyy-MM-dd HH:mm");
    emit replayChanged();
  }

  void syncReplayBounds() {
    if (loaded_.isEmpty() || replayActive_) return;
    if (replayCursorMs_ == 0) replayCursorMs_ = loaded_.last().ms;
  }

  void stepReplay() {
    if (loaded_.isEmpty()) return;
    auto it = std::upper_bound(loaded_.begin(), loaded_.end(), replayCursorMs_, [](qint64 ms, const Candle &c) { return ms < c.ms; });
    if (it == loaded_.end()) {
      replayTimer_.stop();
      emit replayChanged();
      return;
    }
    replayCursorMs_ = it->ms;
    applyReplayView();
    emit replayChanged();
  }

  int replayIntervalForSpeed(int speed) const { return std::max(20, 650 / std::max(1, speed)); }

  void updateMarketSummary() {
    if (loaded_.isEmpty()) return;
    const Candle &last = loaded_.last();
    const Candle &first = loaded_.first();
    const double change = last.close - first.open;
    const double pct = first.open != 0 ? change / first.open * 100.0 : 0.0;
    marketUp_ = change >= 0;
    marketPrice_ = QString::number(last.close, 'f', 2);
    marketChange_ = QString("%1%2 (%3%4%)")
      .arg(change >= 0 ? "+" : "").arg(change, 0, 'f', 2)
      .arg(pct >= 0 ? "+" : "").arg(pct, 0, 'f', 2);
    emit marketChanged();
  }

  void normalizeLoaded() {
    std::sort(loaded_.begin(), loaded_.end(), [](const Candle &a, const Candle &b) { return a.ms < b.ms; });
    loaded_.erase(std::unique(loaded_.begin(), loaded_.end(), [](const Candle &a, const Candle &b) { return a.ms == b.ms; }), loaded_.end());
  }

  void upsertLoadedCandle(const Candle &candle) {
    auto it = std::lower_bound(loaded_.begin(), loaded_.end(), candle.ms, [](const Candle &item, qint64 ms) { return item.ms < ms; });
    if (it != loaded_.end() && it->ms == candle.ms) *it = candle;
    else loaded_.insert(it, candle);
  }

  QString formatTime(qint64 ms, const QString &format) const {
    if (ms <= 0) return "--";
    QTimeZone zone(timeZoneId_);
    if (!zone.isValid()) zone = QTimeZone::systemTimeZone();
    return QDateTime::fromMSecsSinceEpoch(ms, zone).toString(format);
  }

  QString classifyLogLevel(const QString &message) const {
    const QString lower = message.toLower();
    if (message.contains("ERROR") || lower.contains("error") || message.contains("失败") || message.contains("错误")) return "ERROR";
    if (message.contains("WARN") || lower.contains("warn") || message.contains("超时") || message.contains("重连")) return "WARN";
    if (message.startsWith("Chart upsert") || message.contains("DEBUG")) return "DEBUG";
    return "INFO";
  }

  QString deriveLogModule(const QString &message) const {
    const QString lower = message.toLower();
    if (lower.contains("ws") || lower.contains("websocket") || message.contains("实时") || message.contains("推送")) return "WS";
    if (message.startsWith("Chart") || lower.contains("upsert") || message.contains("K线") || lower.contains("candle")) return "Chart";
    if (message.contains("指标") || lower.contains("indicator") || lower.contains("fvg")) return "Indicator";
    if (message.contains("更新") || lower.contains("update") || lower.contains("version")) return "Update";
    if (lower.contains("http") || lower.contains("/api") || lower.contains("api") || message.contains("接口")
        || message.contains("加载") || message.contains("策略") || message.contains("请求")) return "HTTP";
    return "App";
  }

  // ---- Persistence ---------------------------------------------------------
  void loadBackendSettings() {
    QSettings settings("Q4J", "KLineViewer");
    const QString backend = settings.value("backend/http").toString().trimmed();
    if (backend.isEmpty()) return;
    client_.configureBackend(backend, settings.value("backend/ws").toString(),
                             settings.value("backend/realtime", true).toBool());
  }

  void saveBackendSettings() const {
    QSettings settings("Q4J", "KLineViewer");
    settings.setValue("backend/http", client_.backendBase());
    settings.setValue("backend/ws", client_.wsBase());
    settings.setValue("backend/realtime", client_.realtimeEnabled());
  }

  void loadStrategySettings() {
    QSettings settings("Q4J", "KLineViewer");
    setStrategyName(settings.value("strategy/name", "n_in_range_variant").toString());
  }

  void saveStrategySettings() const {
    QSettings settings("Q4J", "KLineViewer");
    settings.setValue("strategy/name", strategyName_);
  }

  void loadDisplaySettings() {
    QSettings settings("Q4J", "KLineViewer");
    timeZoneId_ = settings.value("display/timeZone", QTimeZone::systemTimeZoneId()).toByteArray();
    if (!QTimeZone(timeZoneId_).isValid()) timeZoneId_ = QTimeZone::systemTimeZoneId();
    if (chart_) chart_->setTimeZoneId(timeZoneId_);
  }

  QString timeframeLabel(const QString &token) const {
    static const QHash<QString, QString> labels = {
      {"1S", "1s"}, {"5S", "5s"}, {"15S", "15s"}, {"30S", "30s"},
      {"1M", "1m"}, {"2M", "2m"}, {"3M", "3m"}, {"5M", "5m"}, {"10M", "10m"}, {"15M", "15m"}, {"30M", "30m"},
      {"1H", "1h"}, {"2H", "2h"}, {"4H", "4h"}, {"6H", "6h"}, {"12H", "12h"},
      {"1D", "1D"}, {"1W", "1W"}, {"1MO", "1M"}};
    return labels.value(token, token);
  }

  CandleClient client_;
  ChartItem *chart_ = nullptr;
  LogModel *logModel_ = nullptr;
  StrategyModel *strategyModel_ = nullptr;

  QVector<Candle> loaded_;
  QTimer replayTimer_;

  bool dark_ = true;
  QString symbol_ = "BTCUSD";
  QString timeframe_ = "15M";
  QString strategyName_ = "n_in_range_variant";
  QByteArray timeZoneId_ = QTimeZone::systemTimeZoneId();

  QString connectionStatus_ = "未连接";
  bool connectionLive_ = false;

  QString marketPrice_ = "--";
  QString marketChange_ = "--";
  bool marketUp_ = true;
  QString ohlcText_;
  int eventCount_ = 0;
  QString visibleRangeText_ = "可视范围 --";
  QString dataStartText_ = "数据起点：--";

  bool replayActive_ = false;
  int replaySpeed_ = 10;
  qint64 replayCursorMs_ = 0;
  qint64 replayLoadCursorMs_ = 0;

  bool magnetEnabled_ = false;
  int annotationTool_ = 0;

  QNetworkAccessManager net_;
  bool updateChecking_ = false;
  bool updateAvailable_ = false;
  QString updateStatus_ = "尚未检查";
  QString latestVersion_;
  QString downloadUrl_;
};
