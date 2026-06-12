#pragma once

#include "core.hpp"
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QCursor>
#include <QtQml/qqmlregistration.h>

class ChartItem : public QQuickPaintedItem {
  Q_OBJECT
  QML_ELEMENT

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
  Q_ENUM(AnnotationTool)

  explicit ChartItem(QQuickItem *parent = nullptr) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setFlag(QQuickItem::ItemHasContents, true);
    setActiveFocusOnTab(true);
  }

  QRect rect() const { return QRect(0, 0, qRound(width()), qRound(height())); }

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
      hoveredLineLayerId_.clear();
      selectedLineLayerIds_.clear();
      lineLayerHitboxes_.clear();
    }
    std::sort(candles_.begin(), candles_.end(), [](const Candle &a, const Candle &b) {
      return a.ms < b.ms;
    });
    if (!candles_.isEmpty() && (clearOverlay || visibleCount_ <= rightOffsetBars_ + 8)) {
      const int target = std::min(160, std::max(rightOffsetBars_ + 40, candleCount() + rightOffsetBars_));
      visibleCount_ = std::max(rightOffsetBars_ + 20, target);
      manualPriceScale_ = 1.0;
      manualPriceOffset_ = 0.0;
    } else if (visibleCount_ <= 0) {
      visibleCount_ = std::min(160, std::max(rightOffsetBars_ + 20, candleCount() + rightOffsetBars_));
    }
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
    hoveredLineLayerId_.clear();
    lineLayerHitboxes_.clear();
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
    hoveredLineLayerId_.clear();
    selectedLineLayerIds_.clear();
    lineLayerHitboxes_.clear();
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

  void setReplayMarker(qint64 startMs, bool active) {
    replayMarkerMs_ = startMs;
    replayMarkerActive_ = active;
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
    QQuickPaintedItem::keyPressEvent(event);
  }

  void paint(QPainter *painter) override {
    QPainter &p = *painter;
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
    paintReplayMarker(p);
    paintOverlays(p, minPrice, maxPrice);
    paintIndicators(p, minPrice, maxPrice);
    paintManualAnnotations(p, minPrice, maxPrice);
    paintLatestPriceLine(p, minPrice, maxPrice);
    paintLayerHints(p);
    paintCrosshair(p);
    paintPositionTooltip(p);
    paintOhlcSketch(p);
  }

  void mouseMoveEvent(QMouseEvent *event) override { handlePointerMove(event->position()); }
  void hoverMoveEvent(QHoverEvent *event) override { handlePointerMove(event->position()); }

  void handlePointerMove(const QPointF &posF) {
    mousePos_ = cursorPosition(posF);
    hasMouse_ = true;
    if (annotationDragMode_ != AnnotationDragMode::None) {
      updateAnnotationDrag(posF);
      scheduleRepaint();
      return;
    }
    if (drawingAnnotation_) {
      draftPoint_ = chartPointFromPosition(posF);
      scheduleRepaint();
      return;
    }
    if (dragging_) {
      const int dx = posF.x() - dragStart_.x();
      const int dy = posF.y() - dragStart_.y();
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
      const int dx = posF.x() - dragStart_.x();
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
      const int dy = posF.y() - dragStart_.y();
      manualPriceScale_ = std::clamp(axisPriceScale_ * std::exp(dy / 180.0), 0.2, 8.0);
      scheduleRepaint();
      return;
    }
    hoveredIndex_ = indexAt(mousePos_.x());
    hoveredPositionIndex_ = positionHitboxAt(posF);
    hoveredLineLayerId_ = lineLayerAt(posF);
    if (hoveredIndex_ >= 0 && hoveredIndex_ < candleCount()) {
      emit hoveredCandleChanged(&candles_[hoveredIndex_]);
    } else {
      emit hoveredCandleChanged(nullptr);
    }
    update();
  }

  void hoverLeaveEvent(QHoverEvent *) override {
    hoveredIndex_ = -1;
    hoveredPositionIndex_ = -1;
    hoveredLineLayerId_.clear();
    hasMouse_ = false;
    emit hoveredCandleChanged(nullptr);
    update();
  }

  void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    syncAnnotationStyleToolbar();
  }

  void mousePressEvent(QMouseEvent *event) override {
    forceActiveFocus();
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
      const QString lineLayerId = lineLayerAt(event->position());
      if (!lineLayerId.isEmpty()) {
        if (selectedLineLayerIds_.contains(lineLayerId)) selectedLineLayerIds_.remove(lineLayerId);
        else selectedLineLayerIds_.insert(lineLayerId);
        hoveredLineLayerId_ = lineLayerId;
        update();
        return;
      }
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

  QColor bg() const { return dark_ ? Theme::cBgApp() : QColor("#f6f8fb"); }
  QColor text() const { return dark_ ? Theme::cTextPrimary() : QColor("#172033"); }
  QColor muted() const { return dark_ ? Theme::cTextSecondary() : QColor("#61708a"); }
  QColor grid() const { return dark_ ? Theme::cGrid() : QColor(211, 218, 230, 190); }
  QColor up() const { return Theme::cGreen(); }
  QColor down() const { return Theme::cRed(); }

  QFont uiFont(int pixelSize, QFont::Weight weight = QFont::Normal) const {
    QFont f = QGuiApplication::font();
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
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(dark_ ? QColor(148, 137, 121, 135) : QColor(203, 213, 225, 220), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
    p.setRenderHint(QPainter::Antialiasing, false);
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
    p.setPen(QPen(dark_ ? QColor(148, 137, 121, 170) : QColor(190, 201, 218, 230), 1));
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
    double quantity = std::numeric_limits<double>::quiet_NaN();
    double amount = std::numeric_limits<double>::quiet_NaN();
    double totalR = std::numeric_limits<double>::quiet_NaN();
    QVector<double> tps;
    QString side;
    QString shape;
    QString text;
    QString anchor;
    QString remark;
    QJsonObject data;
    OverlayStyle style;
  };

  struct LineLayerHitbox {
    QString id;
    int zIndex = 0;
    QPolygonF points;
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

  QPen highlightedLinePen(const OverlayStyle &style) const {
    QPen pen(withOpacity(style.stroke, 1.0), std::min(10, std::max(2, style.strokeWidth + 2)));
    if (!style.dash.isEmpty()) pen.setDashPattern(style.dash);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
  }

  QString pointPriceText(double price) const {
    if (!std::isfinite(price)) return "--";
    const double absPrice = std::abs(price);
    const int precision = absPrice >= 100 ? 2 : (absPrice >= 1 ? 4 : 6);
    return QString::number(price, 'f', precision);
  }

  void drawLinePointPriceLabels(QPainter &p, const OverlayLayer &layer, const QPolygonF &screenPoints) {
    if (screenPoints.isEmpty()) return;
    p.save();
    p.setFont(numberFont(std::max(10, layer.style.fontSize), QFont::DemiBold));
    const QFontMetrics fm(p.font());
    const QColor fill = dark_ ? QColor(9, 13, 12, 232) : QColor(255, 253, 247, 238);
    const QColor border = withOpacity(layer.style.stroke, 0.95);
    const QRectF visiblePlot = plotRect().adjusted(-4, -4, 4, 4);
    for (int i = 0; i < screenPoints.size() && i < layer.points.size(); ++i) {
      if (!visiblePlot.contains(screenPoints[i])) continue;
      const QString label = pointPriceText(layer.points[i].price);
      const QSize size = fm.size(Qt::TextSingleLine, label);
      QRectF rect(screenPoints[i].x() + 7, screenPoints[i].y() + (i % 2 == 0 ? -26 : 8), size.width() + 12, 20);
      const QRectF plot = plotRect().adjusted(2, 2, -2, -2);
      if (rect.right() > plot.right()) rect.moveRight(plot.right());
      if (rect.left() < plot.left()) rect.moveLeft(plot.left());
      if (rect.top() < plot.top()) rect.moveTop(screenPoints[i].y() + 8);
      if (rect.bottom() > plot.bottom()) rect.moveBottom(screenPoints[i].y() - 8);
      p.setPen(QPen(border, 1));
      p.setBrush(fill);
      p.drawRect(rect);
      p.setPen(border);
      p.drawText(rect, Qt::AlignCenter, label);
    }
    p.restore();
  }

  void registerLineLayerHitbox(const OverlayLayer &layer, const QPolygonF &screenPoints) {
    if (screenPoints.size() < 2) return;
    LineLayerHitbox hitbox;
    hitbox.id = layer.id;
    hitbox.zIndex = layer.zIndex;
    hitbox.points = screenPoints;
    lineLayerHitboxes_.push_back(hitbox);
  }

  void drawGenericLineLayer(QPainter &p, const OverlayLayer &layer, double minPrice, double maxPrice) {
    if (layer.points.size() < 2) return;
    QPolygonF points;
    for (const OverlayPoint &point : layer.points) points << pointAtTime(point.time, point.price, minPrice, maxPrice);
    const bool emphasized = hoveredLineLayerId_ == layer.id || selectedLineLayerIds_.contains(layer.id);
    p.setPen(emphasized ? highlightedLinePen(layer.style) : layerPen(layer.style));
    p.drawPolyline(points);
    registerLineLayerHitbox(layer, points);
    if (!layer.text.isEmpty()) {
      p.setFont(uiFont(layer.style.fontSize, QFont::DemiBold));
      p.setPen(withOpacity(emphasized ? layer.style.stroke : layer.style.textColor, emphasized ? 1.0 : layer.style.strokeOpacity));
      p.drawText(points.last() + QPointF(6, -6), layer.text);
    }
    if (emphasized) drawLinePointPriceLabels(p, layer, points);
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
    if (std::isfinite(sl) && (isLong ? sl < entry : sl > entry)) {
      drawRangeArea(p, start, end, entry, sl, withOpacity(layer.style.lossFill, layer.style.fillOpacity), minPrice, maxPrice);
      drawLineAt(p, start, end, sl, withOpacity(layer.style.slLine, 0.95), Qt::SolidLine, minPrice, maxPrice);
    }
    double rewardBoundary = std::numeric_limits<double>::quiet_NaN();
    for (const double tp : layer.tps) {
      if (!std::isfinite(tp) || !(isLong ? tp > entry : tp < entry)) continue;
      if (!std::isfinite(rewardBoundary) || std::abs(tp - entry) > std::abs(rewardBoundary - entry)) rewardBoundary = tp;
    }
    if (std::isfinite(rewardBoundary) && (isLong ? rewardBoundary > entry : rewardBoundary < entry)) {
      drawRangeArea(p, start, end, entry, rewardBoundary, withOpacity(layer.style.profitFill, layer.style.fillOpacity), minPrice, maxPrice);
    }
    for (int i = 0; i < layer.tps.size(); ++i) {
      const double tp = layer.tps[i];
      if (!std::isfinite(tp)) continue;
      const bool last = i == layer.tps.size() - 1;
      drawLineAt(p, start, end, tp, withOpacity(layer.style.tpLine, last ? 0.95 : 0.82), last ? Qt::SolidLine : Qt::DashLine, minPrice, maxPrice);
    }
    drawLineAt(p, start, end, entry, withOpacity(layer.style.entryLine, 0.96), Qt::SolidLine, minPrice, maxPrice);
    const QString label = layer.text.isEmpty() ? (isLong ? "L" : "S") : layer.text;
    drawBadgeMarker(p, start, entry, isLong, isLong ? up() : down(), label, minPrice, maxPrice);
    registerGenericPositionHitbox(layer, start, end, minPrice, maxPrice);
  }

  struct AnnotationPoint {
    double index = 0.0;
    double price = 0.0;
  };

  struct AnnotationStyle {
    QColor line = QColor("#DFD0B8");
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
    style.line = dark_ ? QColor("#DFD0B8") : QColor("#008f82");
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
    return dark_ ? QColor(223, 208, 184, 220) : QColor(0, 143, 130, 230);
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
    p.setPen(QPen(QColor("#DFD0B8"), 1.4));
    p.setBrush(QColor("#DFD0B8"));
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
    QDialog dialog(nullptr);
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

  // The floating style toolbar used to be an embedded QFrame child widget.
  // Under Qt Quick it is replaced by a QML overlay, so the in-item widget is
  // no longer built here. Style editing remains available via right-click.
  void buildAnnotationStyleToolbar() {}

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
    annotationStyleToolbar_->move(std::max(54, static_cast<int>((width() - w) / 2)), 18);
    annotationStyleToolbar_->raise();
  }

  void chooseSelectedColor(StyleColorRole role) {
    if (selectedAnnotation_ < 0 || selectedAnnotation_ >= manualAnnotations_.size()) return;
    ManualAnnotation &annotation = manualAnnotations_[selectedAnnotation_];
    QColor current = annotation.style.line;
    if (role == StyleColorRole::Fill) current = annotation.style.fill;
    if (role == StyleColorRole::Profit) current = annotation.style.profit;
    if (role == StyleColorRole::Loss) current = annotation.style.loss;
    const QColor color = QColorDialog::getColor(current, nullptr, "选择颜色");
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

  QVector<double> parsePositionTargets(const QJsonObject &object) const {
    QVector<double> targets;
    auto addTarget = [&](double price) {
      if (!std::isfinite(price)) return;
      for (const double existing : targets) {
        if (std::abs(existing - price) < 1e-9) return;
      }
      targets.push_back(price);
    };
    QJsonArray array = object.value("tps").toArray();
    if (array.isEmpty()) array = object.value("targets").toArray();
    for (const QJsonValue &value : array) {
      if (value.isDouble() || value.isString()) {
        bool ok = false;
        const double price = value.isString() ? value.toString().toDouble(&ok) : value.toDouble();
        if (!value.isString() || ok) addTarget(price);
      } else if (value.isObject()) {
        const QJsonObject target = value.toObject();
        addTarget(layerNumber(target, "price", layerNumber(target, "tp")));
      }
    }
    addTarget(layerNumber(object, "tp1"));
    addTarget(layerNumber(object, "tp2"));
    return targets;
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
      layer.tps = parsePositionTargets(object);
      layer.quantity = layerNumber(object, "quantity");
      layer.amount = layerNumber(object, "amount");
      layer.totalR = layerNumber(object, "totalR", layerNumber(object, "total_r", layerNumber(object, "pnl")));
      layer.side = object.value("side").toString().trimmed().toLower();
      layer.shape = object.value("shape").toString("circle").trimmed().toLower();
      layer.text = object.value("text").toString();
      layer.anchor = object.value("anchor").toString("center").trimmed().toLower();
      layer.remark = object.value("remark").toString();
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
    lineLayerHitboxes_.clear();
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
      if (nVisible_ && type == "HIGH_N_DETECTED") drawNEvent(p, payload.value("n").toObject(), QColor(223, 208, 184, 220), "N", minPrice, maxPrice);
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
      drawRangeArea(p, position.entryTime, end, firstPartialExit, position.exitPrice, QColor(148, 137, 121, 77), minPrice, maxPrice);
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
        drawCircleMarker(p, partial.time, partial.exitPrice, QColor("#DFD0B8"), "TP1", minPrice, maxPrice);
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

  void paintReplayMarker(QPainter &p) {
    if (!replayMarkerActive_ || replayMarkerMs_ <= 0 || candles_.isEmpty()) return;
    const QRectF r = plotRect();
    const double idx = indexForTime(replayMarkerMs_);
    const double x = r.left() + (idx - visibleStart_ + 0.5) * barStep();
    if (x < r.left() - 2 || x > r.right() + 2) return;
    p.save();
    QPen pen(Theme::cBrandBlue(), 1.4, Qt::DashLine);
    p.setPen(pen);
    p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    const QString label = QStringLiteral("回放起点");
    p.setFont(uiFont(10, QFont::DemiBold));
    const QFontMetrics fm(p.font());
    const double w = fm.horizontalAdvance(label) + 12;
    QRectF tag(x - w / 2, r.bottom() + 2, w, 16);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::cBrandBlue());
    p.drawRoundedRect(tag, 3, 3);
    p.setPen(QColor("#FFFFFF"));
    p.drawText(tag, Qt::AlignCenter, label);
    p.restore();
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
    row(indicatorEngine_.fvgCircleSettings().enabled, "FVG Circle");
    for (const IndicatorScript &script : indicatorEngine_.scripts()) {
      row(script.enabled, script.name);
    }
    for (const QString &group : layerGroupOrder_) {
      row(layerGroupVisible_.value(group, true), group);
    }
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
    QVector<double> targetRs;
    double totalR = std::numeric_limits<double>::quiet_NaN();
    double quantity = std::numeric_limits<double>::quiet_NaN();
    QString remark;
    QJsonObject copyData;
  };

  double distanceToSegment(const QPointF &point, const QPointF &a, const QPointF &b) const {
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 1e-9) return std::hypot(point.x() - a.x(), point.y() - a.y());
    const double t = std::clamp(((point.x() - a.x()) * dx + (point.y() - a.y()) * dy) / lengthSquared, 0.0, 1.0);
    const QPointF projection(a.x() + t * dx, a.y() + t * dy);
    return std::hypot(point.x() - projection.x(), point.y() - projection.y());
  }

  QString lineLayerAt(const QPointF &point) const {
    if (!plotRect().contains(point)) return {};
    QString bestId;
    int bestZ = std::numeric_limits<int>::min();
    double bestDistance = std::numeric_limits<double>::infinity();
    constexpr double hitDistance = 8.0;
    for (const LineLayerHitbox &hitbox : lineLayerHitboxes_) {
      if (hitbox.points.size() < 2) continue;
      double distance = std::numeric_limits<double>::infinity();
      for (int i = 1; i < hitbox.points.size(); ++i) {
        distance = std::min(distance, distanceToSegment(point, hitbox.points[i - 1], hitbox.points[i]));
      }
      if (distance > hitDistance) continue;
      if (hitbox.zIndex > bestZ || (hitbox.zIndex == bestZ && distance < bestDistance)) {
        bestId = hitbox.id;
        bestZ = hitbox.zIndex;
        bestDistance = distance;
      }
    }
    return bestId;
  }

  void registerGenericPositionHitbox(const OverlayLayer &layer, qint64 start, qint64 end, double minPrice, double maxPrice) {
    QVector<double> prices;
    auto add = [&](double value) {
      if (std::isfinite(value)) prices.push_back(value);
    };
    add(layer.entry);
    add(layer.sl);
    for (const double tp : layer.tps) add(tp);
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
    if (std::isfinite(risk) && risk > 0) {
      for (const double tp : layer.tps) {
        if (std::isfinite(tp)) hitbox.targetRs.push_back(std::abs(tp - layer.entry) / risk);
      }
    }
    hitbox.totalR = layer.totalR;
    hitbox.quantity = layer.quantity;
    hitbox.remark = layer.remark;
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
    if (std::isfinite(tp1) && risk > 0) hitbox.targetRs.push_back(std::abs(tp1 - entry) / risk);
    if (std::isfinite(exitPrice) && risk > 0) hitbox.targetRs.push_back(std::abs(exitPrice - entry) / risk);
    hitbox.quantity = position.quantity;
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

  QString jsonString(const QString &value) const {
    QJsonArray array;
    array.append(value);
    const QString encoded = QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
    return encoded.mid(1, encoded.size() - 2);
  }

  QString jsonNumberArray(const QVector<double> &values) const {
    QStringList parts;
    for (const double value : values) parts << jsonNumber(value);
    return "[" + parts.join(", ") + "]";
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
      "    \"target_rs\": %8,\n"
      "    \"totalR\": %9,\n"
      "    \"remark\": %10,\n"
      "    \"kline\": [\n"
      "%11\n"
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
      jsonNumberArray(hitbox.targetRs),
      jsonNumber(hitbox.totalR),
      hitbox.remark.isEmpty() ? QString("null") : jsonString(hitbox.remark),
      candleLines.join(",\n")
    );
  }

  bool toggleLayerAt(const QPointF &point) {
    const int customCount = indicatorEngine_.scripts().size();
    const int layerGroupCount = layerGroupOrder_.size();
    const int totalRows = 1 + customCount + layerGroupCount;
    if (point.x() < 8 || point.x() > 230 || point.y() < 10 || point.y() > 14 + totalRows * 24) return false;
    const int row = static_cast<int>((point.y() - 14) / 24);
    if (row == 0) {
      FvgCircleSettings settings = indicatorEngine_.fvgCircleSettings();
      settings.enabled = !settings.enabled;
      indicatorEngine_.setFvgCircleSettings(settings);
      rebuildIndicatorsNow();
      emit fvgCircleVisibilityChanged(settings.enabled);
      return true;
    }
    const int scriptIndex = row - 1;
    if (scriptIndex >= 0 && scriptIndex < indicatorEngine_.scripts().size()) {
      const IndicatorScript script = indicatorEngine_.scripts()[scriptIndex];
      indicatorEngine_.setScriptEnabled(script.id, !script.enabled);
      rebuildIndicatorsNow();
      emit customIndicatorVisibilityChanged(script.id, !script.enabled);
      return true;
    }
    const int groupIndex = row - 1 - customCount;
    if (groupIndex >= 0 && groupIndex < layerGroupOrder_.size()) {
      const QString group = layerGroupOrder_[groupIndex];
      layerGroupVisible_.insert(group, !layerGroupVisible_.value(group, true));
      return true;
    }
    return false;
  }

  void paintCrosshair(QPainter &p) {
    if (hoveredIndex_ < 0 || hoveredIndex_ >= candleCount()) return;
    double minPrice, maxPrice;
    visibleRange(minPrice, maxPrice);
    const QRectF r = plotRect();
    const double x = r.left() + (hoveredIndex_ - visibleStart_ + 0.5) * barStep();
    p.setPen(QPen(dark_ ? QColor(223, 208, 184, 95) : QColor(71, 85, 105, 95), 1, Qt::DashLine));
    p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    if (hasMouse_ && r.contains(mousePos_)) {
      p.drawLine(QPointF(r.left(), mousePos_.y()), QPointF(r.right(), mousePos_.y()));
      drawAxisTag(p, QRectF(r.right() + 6, mousePos_.y() - 9, 66, 18), QString::number(priceForY(mousePos_.y(), minPrice, maxPrice), 'f', 2), QColor("#DFD0B8"));
      const QString time = formatChartTime(candles_[hoveredIndex_].ms, "MM-dd HH:mm");
      drawAxisTag(p, QRectF(x - 48, r.bottom() + 6, 96, 18), time, QColor("#DFD0B8"));
    }
  }

  void drawAxisTag(QPainter &p, const QRectF &rect, const QString &text, const QColor &color) {
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRect(rect);
    p.setPen(dark_ ? QColor("#222831") : QColor("#111813"));
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

  QString formatTargetRs(const QVector<double> &targetRs) const {
    if (targetRs.isEmpty()) return "--";
    QStringList values;
    const int visibleCount = std::min(4, static_cast<int>(targetRs.size()));
    for (int i = 0; i < visibleCount; ++i) {
      values << QString("TP%1 %2R").arg(i + 1).arg(formatCompact(targetRs[i]));
    }
    if (targetRs.size() > visibleCount) values << QString("+%1").arg(targetRs.size() - visibleCount);
    return values.join(" / ");
  }

  QStringList wrapTooltipText(const QString &text, const QFontMetrics &fm, int maxWidth, int maxLines) const {
    QString normalized = text;
    normalized.replace("\r\n", "\n");
    normalized.replace('\r', '\n');
    QStringList lines;
    bool clipped = false;
    auto appendLine = [&](const QString &line) {
      if (lines.size() >= maxLines) {
        clipped = true;
        return false;
      }
      lines << line;
      return true;
    };
    const QStringList paragraphs = normalized.split('\n');
    for (const QString &paragraph : paragraphs) {
      QString line;
      for (const QChar ch : paragraph) {
        const QString candidate = line + ch;
        if (!line.isEmpty() && fm.horizontalAdvance(candidate) > maxWidth) {
          if (!appendLine(line.trimmed())) break;
          line = QString(ch);
        } else {
          line = candidate;
        }
      }
      if (clipped) break;
      if (!line.isEmpty() || paragraph.isEmpty()) {
        if (!appendLine(line.trimmed())) break;
      }
    }
    if (clipped && !lines.isEmpty()) {
      QString last = lines.last();
      while (!last.isEmpty() && fm.horizontalAdvance(last + "...") > maxWidth) last.chop(1);
      lines.last() = last.trimmed() + "...";
    }
    return lines;
  }

  void paintPositionTooltip(QPainter &p) {
    if (!hasMouse_ || hoveredPositionIndex_ < 0 || hoveredPositionIndex_ >= positionHitboxes_.size()) return;
    const PositionHitbox &hitbox = positionHitboxes_[hoveredPositionIndex_];
    const int panelWidth = 340;
    const int rowHeight = 18;
    QVector<QPair<QString, QString>> rows;
    rows << qMakePair(QString("Entry"), formatCompact(hitbox.entry));
    rows << qMakePair(QString("SL"), formatCompact(hitbox.sl));
    rows << qMakePair(QString("1R TP"), formatCompact(hitbox.oneRtp));
    rows << qMakePair(QString("Targets R"), formatTargetRs(hitbox.targetRs));
    rows << qMakePair(QString("Total R"), std::isfinite(hitbox.totalR) ? QString("%1R").arg(formatCompact(hitbox.totalR)) : QString("--"));

    QFont rowFont = uiFont(11, QFont::Medium);
    QFont remarkFont = uiFont(10, QFont::Medium);
    p.setFont(remarkFont);
    const QStringList remarkLines = hitbox.remark.trimmed().isEmpty()
      ? QStringList()
      : wrapTooltipText(hitbox.remark.trimmed(), QFontMetrics(remarkFont), panelWidth - 24, 5);
    const int remarkHeight = remarkLines.isEmpty() ? 0 : 24 + remarkLines.size() * 16;
    const int panelHeight = 58 + rows.size() * rowHeight + remarkHeight + 12;
    QRectF panel(mousePos_.x() + 14, mousePos_.y() + 14, panelWidth, panelHeight);
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

    p.setFont(rowFont);
    double y = panel.top() + 60;
    for (int i = 0; i < rows.size(); ++i) {
      p.setPen(muted());
      p.drawText(QPointF(panel.left() + 12, y), rows[i].first);
      p.setPen(rows[i].first == "Total R" && std::isfinite(hitbox.totalR) ? (hitbox.totalR >= 0 ? up() : down()) : text());
      p.drawText(QRectF(panel.left() + 96, y - 13, panel.width() - 108, 17), Qt::AlignRight | Qt::AlignVCenter, rows[i].second);
      y += rowHeight;
    }
    if (!remarkLines.isEmpty()) {
      y += 4;
      p.setPen(QPen(dark_ ? QColor(230, 226, 211, 28) : QColor(23, 31, 27, 30), 1));
      p.drawLine(QPointF(panel.left() + 12, y), QPointF(panel.right() - 12, y));
      y += 16;
      p.setFont(uiFont(10, QFont::DemiBold));
      p.setPen(muted());
      p.drawText(QPointF(panel.left() + 12, y), "Remark");
      y += 16;
      p.setFont(remarkFont);
      p.setPen(text());
      for (const QString &line : remarkLines) {
        p.drawText(QRectF(panel.left() + 12, y - 12, panel.width() - 24, 16), Qt::AlignLeft | Qt::AlignVCenter, line);
        y += 16;
      }
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
    p.setPen(QColor("#DFD0B8"));
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
    p.setPen(QColor("#DFD0B8"));
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
  QString hoveredLineLayerId_;
  QSet<QString> selectedLineLayerIds_;
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
  QVector<LineLayerHitbox> lineLayerHitboxes_;
  AnnotationTool annotationTool_ = AnnotationTool::None;
  bool magnetEnabled_ = true;
  qint64 replayMarkerMs_ = 0;
  bool replayMarkerActive_ = false;
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

inline bool ChartItem::showPositionContextMenu(const QPointF &pos, const QPoint &globalPos) {
  const int index = positionHitboxAt(pos);
  if (index < 0 || index >= positionHitboxes_.size()) return false;
  QMenu menu;
  QAction *copy = menu.addAction("复制区块信息");
  QAction *chosen = menu.exec(globalPos);
  if (chosen == copy) {
    QApplication::clipboard()->setText(positionCopyJson(positionHitboxes_[index]));
  }
  return true;
}
