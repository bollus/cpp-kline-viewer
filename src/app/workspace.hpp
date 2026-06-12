#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QSettings>
#include <QTimeZone>

#include "app_controller.hpp"
#include "models.hpp"

// Top-level facade that the QML chrome binds to as `controller`. It owns the
// shared log / strategy models and the global symbol / backend / theme state,
// and multiplexes every other property/command to the currently active chart
// pane. It also coordinates the multi-view layout and cross-pane crosshair /
// viewport synchronisation.
//
// Each pane owns a ChartItem (created in QML); the matching AppController is
// created and owned here and bound to that chart via attachChart()/detachChart().
class Workspace : public QObject {
  Q_OBJECT

  // ---- Global state --------------------------------------------------------
  Q_PROPERTY(LogModel *logModel READ logModel CONSTANT)
  Q_PROPERTY(StrategyModel *strategyModel READ strategyModel CONSTANT)
  Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY darkChanged)
  Q_PROPERTY(QString symbol READ symbol WRITE setSymbol NOTIFY symbolChanged)

  Q_PROPERTY(bool backendConfigured READ backendConfigured NOTIFY backendChanged)
  Q_PROPERTY(QString backendBase READ backendBase NOTIFY backendChanged)
  Q_PROPERTY(QString wsBase READ wsBase NOTIFY backendChanged)
  Q_PROPERTY(bool realtimeEnabled READ realtimeEnabled NOTIFY backendChanged)

  // ---- Layout / views ------------------------------------------------------
  Q_PROPERTY(QString layoutId READ layoutId WRITE setLayout NOTIFY layoutChanged)
  Q_PROPERTY(int viewCount READ viewCount NOTIFY layoutChanged)
  Q_PROPERTY(int activeIndex READ activeIndex NOTIFY activeChanged)

  // ---- Forwarded from the active pane -------------------------------------
  Q_PROPERTY(QString timeframe READ timeframe NOTIFY timeframeChanged)
  Q_PROPERTY(QString timeframeLabel READ timeframeLabel NOTIFY timeframeChanged)
  Q_PROPERTY(QString strategyName READ strategyName NOTIFY strategyNameChanged)
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

  Q_PROPERTY(bool magnetEnabled READ magnetEnabled WRITE setMagnetEnabled NOTIFY magnetChanged)
  Q_PROPERTY(int annotationTool READ annotationTool WRITE setAnnotationTool NOTIFY annotationToolChanged)

  Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
  Q_PROPERTY(QString updateStatus READ updateStatus NOTIFY updateChanged)
  Q_PROPERTY(bool updateChecking READ updateChecking NOTIFY updateChanged)
  Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateChanged)
  Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY updateChanged)

public:
  explicit Workspace(QObject *parent = nullptr) : QObject(parent) {
    logModel_ = new LogModel(this);
    strategyModel_ = new StrategyModel(this);
    paneTimeframes_ = {"15M", "1H", "4H", "1D", "1W", "30M", "5M", "1M"};
  }

  LogModel *logModel() const { return logModel_; }
  StrategyModel *strategyModel() const { return strategyModel_; }

  bool dark() const { return dark_; }
  void setDark(bool value) {
    if (dark_ == value) return;
    dark_ = value;
    for (AppController *vc : views_) vc->setDark(value);
    emit darkChanged();
  }

  QString symbol() const { return symbol_; }
  void setSymbol(const QString &value) {
    if (symbol_ == value || value.trimmed().isEmpty()) return;
    symbol_ = value.trimmed();
    saveSymbol();
    for (AppController *vc : views_) { vc->setSymbol(symbol_); vc->refresh(); }
    emit symbolChanged();
  }

  bool backendConfigured() const { return !http_.isEmpty(); }
  QString backendBase() const { return http_; }
  QString wsBase() const { return ws_; }
  bool realtimeEnabled() const { return realtime_; }

  QString layoutId() const { return layoutId_; }
  void setLayout(const QString &id) {
    const QString resolved = layoutViewCount(id) > 0 ? id : QStringLiteral("1");
    if (layoutId_ == resolved) return;
    layoutId_ = resolved;
    emit layoutChanged();
  }
  int viewCount() const { return layoutViewCount(layoutId_); }
  int activeIndex() const { return activeSlot_; }

  // ---- Forwarded accessors (safe when no active pane) ---------------------
  QString timeframe() const { return active_ ? active_->timeframe() : QStringLiteral("15M"); }
  QString timeframeLabel() const { return active_ ? active_->timeframeLabelCurrent() : QStringLiteral("15m"); }
  QString strategyName() const { return active_ ? active_->strategyName() : QString(); }
  QString connectionStatus() const { return active_ ? active_->connectionStatus() : QStringLiteral("未连接"); }
  bool connectionLive() const { return active_ ? active_->connectionLive() : false; }
  QString marketPrice() const { return active_ ? active_->marketPrice() : QStringLiteral("--"); }
  QString marketChange() const { return active_ ? active_->marketChange() : QStringLiteral("--"); }
  bool marketUp() const { return active_ ? active_->marketUp() : true; }
  QString ohlcText() const { return active_ ? active_->ohlcText() : QString(); }
  int eventCount() const { return active_ ? active_->eventCount() : 0; }
  QString visibleRangeText() const { return active_ ? active_->visibleRangeText() : QStringLiteral("可视范围 --"); }
  QString dataStartText() const { return active_ ? active_->dataStartText() : QStringLiteral("数据起点：--"); }

  bool replayActive() const { return active_ ? active_->replayActive() : false; }
  void setReplayActive(bool a) { if (active_) active_->setReplayActive(a); }
  bool replayPlaying() const { return active_ ? active_->replayPlaying() : false; }
  int replaySpeed() const { return active_ ? active_->replaySpeed() : 10; }
  void setReplaySpeed(int s) { if (active_) active_->setReplaySpeed(s); }
  double replayProgress() const { return active_ ? active_->replayProgress() : 0.0; }
  QString replayCursorText() const { return active_ ? active_->replayCursorText() : QStringLiteral("--"); }

  bool magnetEnabled() const { return active_ ? active_->magnetEnabled() : false; }
  void setMagnetEnabled(bool v) { if (active_) active_->setMagnetEnabled(v); }
  int annotationTool() const { return active_ ? active_->annotationTool() : 0; }
  void setAnnotationTool(int t) { if (active_) active_->setAnnotationTool(t); }

  QString appVersion() const { return active_ ? active_->appVersion() : QStringLiteral(Q4J_APP_VERSION); }
  QString updateStatus() const { return active_ ? active_->updateStatus() : QStringLiteral("尚未检查"); }
  bool updateChecking() const { return active_ ? active_->updateChecking() : false; }
  bool updateAvailable() const { return active_ ? active_->updateAvailable() : false; }
  QString latestVersion() const { return active_ ? active_->latestVersion() : QString(); }

  // ---- Bootstrap -----------------------------------------------------------
  Q_INVOKABLE void start() {
    loadGlobalSettings();
    started_ = true;
    for (auto it = views_.constBegin(); it != views_.constEnd(); ++it)
      configureView(it.value(), it.key());
    requestStrategiesOnce();
  }

  // ---- View registry (called from QML panes) ------------------------------
  //
  // The AppController is *owned by C++* (created here, not instantiated in QML)
  // so we never depend on AppController being a creatable QML type. The pane
  // only creates its ChartItem and hands it over; we wire everything else up
  // and return the controller so the pane can read its per-view properties.
  Q_INVOKABLE QObject *attachChart(int slot, QObject *chartObj) {
    if (slot < 0) return nullptr;
    ChartItem *chart = qobject_cast<ChartItem *>(chartObj);

    AppController *vc = views_.value(slot, nullptr);
    if (!vc) {
      vc = new AppController(this);
      views_.insert(slot, vc);
      vc->setSharedModels(logModel_, strategyModel_);

      connect(vc, &AppController::viewportChanged, this,
              [this, vc](qint64 s, qint64 e) { onPanned(vc, s, e); });
      connect(vc, &AppController::crosshairChanged, this,
              [this, vc](qint64 ms, bool a) { onCrosshair(vc, ms, a); });
      connect(vc, &AppController::dataLoaded, this, [this, vc] { onViewLoaded(vc); });
      connect(vc, &AppController::activated, this, [this, vc] { activateController(vc); });
      connect(vc, &AppController::timeframeChanged, this, [this, slot, vc] {
        if (slot >= 0 && slot < paneTimeframes_.size()) paneTimeframes_[slot] = vc->timeframe();
      });
    }

    vc->setChart(chart);
    if (started_) configureView(vc, slot);
    if (!active_) activateController(vc);
    if (started_) requestStrategiesOnce();
    emit layoutChanged();
    return vc;
  }

  Q_INVOKABLE void detachChart(int slot) {
    AppController *vc = views_.value(slot, nullptr);
    if (!vc) return;
    disconnect(vc, nullptr, this, nullptr);
    views_.remove(slot);
    if (active_ == vc) {
      disconnectActive();
      active_ = nullptr;
      activeSlot_ = -1;
      if (!views_.isEmpty()) {
        auto it = views_.constBegin();
        activeSlot_ = it.key();
        active_ = it.value();
        connectActive();
        emitActiveForwarded();
        emit activeChanged();
      }
    }
    vc->setChart(nullptr);  // stop painting into the about-to-be-destroyed item
    vc->deleteLater();
    emit layoutChanged();
  }

  Q_INVOKABLE void setActiveIndex(int slot) {
    AppController *vc = views_.value(slot, nullptr);
    if (vc) activateController(vc);
  }

  Q_INVOKABLE QString timeframeForSlot(int slot) const {
    if (slot >= 0 && slot < paneTimeframes_.size()) return paneTimeframes_[slot];
    return QStringLiteral("15M");
  }

  Q_INVOKABLE void addView() {
    const int n = viewCount();
    setLayout(n <= 1 ? "2h" : n == 2 ? "3h" : "4");
  }
  Q_INVOKABLE void removeView() {
    const int n = viewCount();
    setLayout(n >= 4 ? "3h" : n == 3 ? "2h" : "1");
  }

  // ---- Commands forwarded to the active pane ------------------------------
  Q_INVOKABLE void refresh() { if (active_) active_->refresh(); }
  Q_INVOKABLE void setTimeframe(const QString &token) { if (active_) active_->setTimeframe(token); }
  Q_INVOKABLE void loadStrategy(const QString &name) { if (active_) active_->loadStrategy(name); }
  Q_INVOKABLE void loadStrategies() { if (active_) active_->loadStrategies(); }
  Q_INVOKABLE void replayPlayPause() { if (active_) active_->replayPlayPause(); }
  Q_INVOKABLE void replayStepForward() { if (active_) active_->replayStepForward(); }
  Q_INVOKABLE void replayStepBackward() { if (active_) active_->replayStepBackward(); }
  Q_INVOKABLE void replayJumpToStart() { if (active_) active_->replayJumpToStart(); }
  Q_INVOKABLE void replayJumpToEnd() { if (active_) active_->replayJumpToEnd(); }
  Q_INVOKABLE void replaySeek(double p) { if (active_) active_->replaySeek(p); }
  Q_INVOKABLE void setReplayCursorText(const QString &t) { if (active_) active_->setReplayCursorText(t); }
  Q_INVOKABLE void checkForUpdates() { if (active_) active_->checkForUpdates(); }
  Q_INVOKABLE void openDownloadPage() { if (active_) active_->openDownloadPage(); }

  // ---- Backend (global, fans out to every pane) ---------------------------
  Q_INVOKABLE void configureBackend(const QString &http, const QString &ws, bool realtime) {
    const QString trimmed = http.trimmed();
    if (trimmed.isEmpty()) return;
    http_ = trimmed;
    ws_ = ws.trimmed();
    realtime_ = realtime;
    saveBackendSettings();
    for (auto it = views_.constBegin(); it != views_.constEnd(); ++it)
      it.value()->configureBackendSilent(http_, ws_, realtime_);
    strategiesRequested_ = false;
    requestStrategiesOnce();
    emit backendChanged();
  }

signals:
  void darkChanged();
  void symbolChanged();
  void backendChanged();
  void layoutChanged();
  void activeChanged();
  void timeframeChanged();
  void strategyNameChanged();
  void statusChanged();
  void marketChanged();
  void ohlcChanged();
  void eventCountChanged();
  void visibleRangeChanged();
  void replayChanged();
  void magnetChanged();
  void annotationToolChanged();
  void updateChanged();

private:
  static int layoutViewCount(const QString &id) {
    if (id == "1") return 1;
    if (id == "2h" || id == "2v") return 2;
    if (id == "3h" || id == "3lr") return 3;
    if (id == "4") return 4;
    return 0;
  }

  void configureView(AppController *vc, int slot) {
    vc->setDark(dark_);
    vc->setSymbol(symbol_);
    vc->setTimeframeQuiet(timeframeForSlot(slot));
    if (!http_.isEmpty()) vc->configureBackendSilent(http_, ws_, realtime_);
    else vc->refresh();
  }

  void requestStrategiesOnce() {
    if (strategiesRequested_ || http_.isEmpty() || !active_) return;
    strategiesRequested_ = true;
    active_->loadOverlayStrategiesOnce();
  }

  void activateController(AppController *vc) {
    if (active_ == vc) return;
    disconnectActive();
    active_ = vc;
    activeSlot_ = views_.key(vc, -1);
    connectActive();
    emit activeChanged();
    emitActiveForwarded();
  }

  void connectActive() {
    if (!active_) return;
    activeConns_ << connect(active_, &AppController::timeframeChanged, this, &Workspace::timeframeChanged);
    activeConns_ << connect(active_, &AppController::strategyNameChanged, this, &Workspace::strategyNameChanged);
    activeConns_ << connect(active_, &AppController::statusChanged, this, &Workspace::statusChanged);
    activeConns_ << connect(active_, &AppController::marketChanged, this, &Workspace::marketChanged);
    activeConns_ << connect(active_, &AppController::ohlcChanged, this, &Workspace::ohlcChanged);
    activeConns_ << connect(active_, &AppController::eventCountChanged, this, &Workspace::eventCountChanged);
    activeConns_ << connect(active_, &AppController::visibleRangeChanged, this, &Workspace::visibleRangeChanged);
    activeConns_ << connect(active_, &AppController::replayChanged, this, &Workspace::replayChanged);
    activeConns_ << connect(active_, &AppController::magnetChanged, this, &Workspace::magnetChanged);
    activeConns_ << connect(active_, &AppController::annotationToolChanged, this, &Workspace::annotationToolChanged);
    activeConns_ << connect(active_, &AppController::updateChanged, this, &Workspace::updateChanged);
  }

  void disconnectActive() {
    for (const QMetaObject::Connection &c : activeConns_) disconnect(c);
    activeConns_.clear();
  }

  void emitActiveForwarded() {
    emit timeframeChanged();
    emit strategyNameChanged();
    emit statusChanged();
    emit marketChanged();
    emit ohlcChanged();
    emit eventCountChanged();
    emit visibleRangeChanged();
    emit replayChanged();
    emit magnetChanged();
    emit annotationToolChanged();
    emit updateChanged();
  }

  // ---- Sync handlers -------------------------------------------------------
  void onPanned(AppController *source, qint64 startMs, qint64 endMs) {
    if (syncing_ || startMs <= 0 || endMs <= startMs) return;
    syncing_ = true;
    lastStartMs_ = startMs;
    lastEndMs_ = endMs;
    hasRange_ = true;
    for (AppController *vc : views_)
      if (vc != source) vc->applySyncedRange(startMs, endMs);
    syncing_ = false;
  }

  void onCrosshair(AppController *source, qint64 ms, bool active) {
    for (AppController *vc : views_)
      if (vc != source) vc->applySyncedCrosshair(ms, active);
  }

  void onViewLoaded(AppController *vc) {
    // Align a freshly loaded pane to the shared time window instead of letting
    // it push its own default range onto the others.
    if (hasRange_ && vc != active_) vc->applySyncedRange(lastStartMs_, lastEndMs_);
  }

  // ---- Persistence ---------------------------------------------------------
  void loadGlobalSettings() {
    QSettings s("Q4J", "KLineViewer");
    http_ = s.value("backend/http").toString().trimmed();
    ws_ = s.value("backend/ws").toString().trimmed();
    realtime_ = s.value("backend/realtime", true).toBool();
    symbol_ = s.value("symbol", symbol_).toString();
    emit backendChanged();
    emit symbolChanged();
  }
  void saveBackendSettings() const {
    QSettings s("Q4J", "KLineViewer");
    s.setValue("backend/http", http_);
    s.setValue("backend/ws", ws_);
    s.setValue("backend/realtime", realtime_);
  }
  void saveSymbol() const {
    QSettings s("Q4J", "KLineViewer");
    s.setValue("symbol", symbol_);
  }

  LogModel *logModel_ = nullptr;
  StrategyModel *strategyModel_ = nullptr;

  QHash<int, AppController *> views_;
  AppController *active_ = nullptr;
  int activeSlot_ = -1;
  QList<QMetaObject::Connection> activeConns_;

  QString layoutId_ = "1";
  QStringList paneTimeframes_;

  bool dark_ = true;
  QString symbol_ = "XAUUSD";
  QString http_;
  QString ws_;
  bool realtime_ = true;

  bool started_ = false;
  bool strategiesRequested_ = false;
  bool syncing_ = false;
  bool hasRange_ = false;
  qint64 lastStartMs_ = 0;
  qint64 lastEndMs_ = 0;
};
