#pragma once

#include "core.hpp"

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

  ~CandleClient() override {
    // Members destruct in reverse declaration order: liveWatchdog_ dies before
    // socket_. socket_'s destructor calls disconnectFromHost() which emits
    // disconnected(), and our slot there touches the already-destroyed
    // liveWatchdog_ -> use-after-free. Sever the socket's signals up front (all
    // members are still alive here) so no slot runs during teardown.
    socket_.disconnect();
    liveWatchdog_.stop();
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

  void load(const QString &symbol, const QString &interval, const QString &strategyName) {
    symbol_ = symbol.trimmed();
    interval_ = interval.trimmed().toLower();
    strategyName_ = normalizedStrategyName(strategyName);
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
    if (symbol_.isEmpty() || interval_.isEmpty()) {
      emit overlayEventsLoaded(QJsonArray{});
      return;
    }
    QUrl url(backendBase_ + "/api/strategy-overlay-events");
    QUrlQuery query;
    query.addQueryItem("strategy", normalizedStrategyName(strategyName_));
    query.addQueryItem("symbol", symbol_);
    query.addQueryItem("interval", interval_);
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
    // emitDebugLog(QString("WS parsed candle: %1 O=%2 H=%3 L=%4 C=%5")
    //   .arg(QDateTime::fromMSecsSinceEpoch(candle.ms).toString("yyyy-MM-dd HH:mm:ss.zzz"))
    //   .arg(candle.open)
    //   .arg(candle.high)
    //   .arg(candle.low)
    //   .arg(candle.close));
    emit statusChanged("实时", true);
    // emitDebugLog("WS emit candleUpdated");
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
