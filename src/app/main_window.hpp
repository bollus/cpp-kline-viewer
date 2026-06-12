#pragma once

#include "chart_widget.hpp"
#include "candle_client.hpp"

#include <QtSvg/QSvgRenderer>
#include <QStatusBar>

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow() {
    setWindowTitle("AlgoHub 量化复盘终端");
    setWindowIcon(QIcon(":/app.ico"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setMouseTracking(true);
    resize(1480, 900);
    buildUi();
    bindSignals();
    applyTheme();
    setReplaySpeed(replaySpeedValue_);
    loadBackendSettings();
    loadStrategySettings();
    if (client_.hasConfiguredBackend()) client_.loadOverlayStrategies();
    loadIndicatorSettings();
    loadDisplaySettings();
    updateLegendSymbol();
    syncServerForm();
    updateServerInfoLabel();
    loadLayoutSettings();
    updateStatusBar();
    if (client_.hasConfiguredBackend()) {
      refresh();
    } else {
      setConnectionStatus("后端未配置", false);
      promptConfigureBackend();
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
    if (watched == centralWidget() || watched == chart_) {
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
    if (titleBar_) titleBar_->unsetCursor();
  }

  void applyResizeCursor(const QCursor &cursor) {
    setCursor(cursor);
    if (centralWidget()) centralWidget()->setCursor(cursor);
    if (chart_) chart_->setCursor(cursor);
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

  // Render an SVG resource into a tinted pixmap. Composition SourceIn keeps the
  // alpha shape of the icon and recolours every opaque pixel with `color`, so a
  // single monochrome asset adapts to theme/state colours.
  QPixmap tintedPixmap(const QString &name, const QColor &color, int px = 22) const {
    const qreal dpr = devicePixelRatioF() > 0 ? devicePixelRatioF() : 1.0;
    QSvgRenderer renderer(QString(":/icons/%1.svg").arg(name));
    QPixmap pm(QSize(px, px) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (renderer.isValid()) renderer.render(&p, QRectF(0, 0, px, px));
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(QRect(0, 0, px, px), color);
    p.end();
    return pm;
  }

  // Build a multi-state icon (normal / hover-active / checked) from one asset.
  QIcon themedIcon(const QString &name, int px = 22) const {
    const QColor base = dark_ ? Theme::cTextSecondary() : QColor(Theme::lTextSecondary());
    const QColor active = dark_ ? Theme::cTextPrimary() : QColor(Theme::lTextPrimary());
    const QColor checked = dark_ ? Theme::cBrandBlue() : QColor(Theme::lBrandBlue());
    QIcon icon;
    icon.addPixmap(tintedPixmap(name, base, px), QIcon::Normal, QIcon::Off);
    icon.addPixmap(tintedPixmap(name, active, px), QIcon::Active, QIcon::Off);
    icon.addPixmap(tintedPixmap(name, checked, px), QIcon::Normal, QIcon::On);
    icon.addPixmap(tintedPixmap(name, checked, px), QIcon::Active, QIcon::On);
    icon.addPixmap(tintedPixmap(name, base, px), QIcon::Disabled, QIcon::Off);
    return icon;
  }

  // Track icon-backed buttons so applyTheme() can recolour them on theme switch.
  // The icon name lives on the widget as a property so tool-group buttons can
  // swap which glyph they show (e.g. "last used" tool) and still be re-themed.
  void registerIcon(QAbstractButton *button, const QString &name, int px = 22) {
    button->setProperty("iconName", name);
    button->setProperty("iconPx", px);
    if (!iconButtons_.contains(button)) iconButtons_.append(button);
    button->setIcon(themedIcon(name, px));
  }

  void refreshIcons() {
    for (QAbstractButton *button : iconButtons_) {
      if (!button) continue;
      const QString name = button->property("iconName").toString();
      const int px = button->property("iconPx").toInt();
      if (!name.isEmpty()) button->setIcon(themedIcon(name, px > 0 ? px : 22));
    }
  }

  QIcon annotationIcon(ChartWidget::AnnotationTool tool) const {
    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor fg = dark_ ? QColor("#f4efe3") : QColor("#131916");
    const QColor accent = QColor("#DFD0B8");
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

  // A single tool inside a collapsible group. `tool` < 0 marks a placeholder
  // (reserved in the UI but not wired to chart behaviour yet).
  struct ToolItem {
    QString icon;
    QString tip;
    int tool = -1;
  };

  // Apply a tool item to a group's main button: swap glyph/tooltip, remember the
  // selection ("last used"), and -- when activating -- drive the chart tool.
  void selectGroupTool(QToolButton *button, const ToolItem &item, bool activate) {
    button->setProperty("iconName", item.icon);
    button->setProperty("iconPx", 22);
    button->setProperty("tool", item.tool);
    button->setIcon(themedIcon(item.icon, 22));
    button->setToolTip(item.tip + (item.tool < 0 ? "（即将支持）" : ""));
    button->setEnabled(item.tool >= 0);
    if (activate && item.tool >= 0) activateDrawingTool(button);
  }

  void activateDrawingTool(QToolButton *button) {
    const int tool = button->property("tool").toInt();
    for (QToolButton *b : drawingButtons_) {
      QSignalBlocker blocker(b);
      b->setChecked(b == button);
    }
    chart_->setAnnotationTool(static_cast<ChartWidget::AnnotationTool>(tool));
  }

  // Sync toolbar checked-state when the chart reports a tool change.
  void syncDrawingTool(ChartWidget::AnnotationTool tool) {
    for (QToolButton *b : drawingButtons_) {
      const bool match = b->property("tool").toInt() == static_cast<int>(tool);
      QSignalBlocker blocker(b);
      b->setChecked(match);
    }
  }

  QToolButton *addToolGroup(QVBoxLayout *layout, const QVector<ToolItem> &items) {
    auto *button = new QToolButton;
    button->setObjectName("annotationToolButton");
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setFixedSize(40, 40);
    button->setIconSize(QSize(22, 22));
    if (items.size() > 1) {
      auto *menu = new QMenu(button);
      for (const ToolItem &item : items) {
        QAction *act = menu->addAction(themedIcon(item.icon, 18), item.tip);
        act->setEnabled(item.tool >= 0);
        connect(act, &QAction::triggered, this, [this, button, item] {
          selectGroupTool(button, item, true);
        });
      }
      button->setMenu(menu);
      button->setPopupMode(QToolButton::MenuButtonPopup);
    } else {
      button->setPopupMode(QToolButton::DelayedPopup);
    }
    // Default = first enabled item, otherwise the first (placeholder) item.
    ToolItem def = items.first();
    for (const ToolItem &item : items) {
      if (item.tool >= 0) { def = item; break; }
    }
    selectGroupTool(button, def, false);
    connect(button, &QToolButton::clicked, this, [this, button] {
      if (button->property("tool").toInt() >= 0) activateDrawingTool(button);
    });
    drawingButtons_.append(button);
    iconButtons_.append(button);
    layout->addWidget(button, 0, Qt::AlignHCenter);
    return button;
  }

  QFrame *toolbarDivider() {
    auto *divider = new QFrame;
    divider->setObjectName("annotationDivider");
    divider->setFixedSize(24, 1);
    return divider;
  }

  QFrame *buildAnnotationToolbar() {
    using T = ChartWidget::AnnotationTool;
    auto *toolbar = new QFrame;
    toolbar->setObjectName("annotationToolbar");
    toolbar->setFixedWidth(52);
    auto *layout = new QVBoxLayout(toolbar);
    layout->setContentsMargins(6, 8, 6, 8);
    layout->setSpacing(6);
    layout->setAlignment(Qt::AlignHCenter);

    QToolButton *select = addToolGroup(layout, {
      {"cursor", "选择 / 拖动图表", static_cast<int>(T::None)},
      {"crosshair", "十字光标", -1},
    });
    layout->addWidget(toolbarDivider());
    addToolGroup(layout, {
      {"trend-line", "趋势线 / 单向横线：拖拽确定长度", static_cast<int>(T::SegmentLine)},
      {"h-line", "水平线：全屏", static_cast<int>(T::HorizontalLine)},
      {"v-line", "垂直线：全屏", static_cast<int>(T::VerticalLine)},
      {"brush", "折线 / 画笔：连续点击，右键或双击结束", static_cast<int>(T::Polyline)},
    });
    addToolGroup(layout, {
      {"rectangle", "矩形方框", static_cast<int>(T::Rectangle)},
      {"fibonacci", "斐波那契回撤", -1},
    });
    addToolGroup(layout, {
      {"long", "开仓区块 Long", static_cast<int>(T::LongBlock)},
      {"short", "开仓区块 Short", static_cast<int>(T::ShortBlock)},
    });
    addToolGroup(layout, {
      {"arrow", "箭头标注", -1},
      {"text", "文本标注", -1},
      {"ruler", "测量 / 尺子", -1},
    });
    layout->addWidget(toolbarDivider());
    magnetButton_ = new QToolButton;
    magnetButton_->setObjectName("annotationToolButton");
    magnetButton_->setCheckable(true);
    magnetButton_->setChecked(true);
    magnetButton_->setAutoRaise(true);
    magnetButton_->setFixedSize(40, 40);
    magnetButton_->setIconSize(QSize(22, 22));
    magnetButton_->setToolTip("磁铁吸附：吸附到最近 K 线 OHLC");
    registerIcon(magnetButton_, "magnet", 22);
    layout->addWidget(magnetButton_, 0, Qt::AlignHCenter);
    addToolGroup(layout, {
      {"lock", "锁定绘图", -1},
      {"eye", "显示 / 隐藏绘图", -1},
      {"trash", "删除绘图", -1},
    });
    layout->addStretch(1);

    // Cursor tool is the initial selection.
    QSignalBlocker blocker(select);
    select->setChecked(true);
    return toolbar;
  }

  void buildUi() {
    auto *root = new QWidget;
    root->setObjectName("appShell");
    root->setMouseTracking(true);
    root->installEventFilter(this);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    setCentralWidget(root);

    strategy_ = new QComboBox(this);
    strategy_->setVisible(false);
    strategy_->addItem("n_in_range_variant");

    buildTopBar();
    layout->addWidget(titleBar_);

    mainSplitter_ = new QSplitter(Qt::Vertical);
    mainSplitter_->setObjectName("mainSplitter");
    mainSplitter_->setHandleWidth(4);
    mainSplitter_->setChildrenCollapsible(false);

    auto *body = new QWidget;
    body->setObjectName("bodyArea");
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(8, 8, 8, 6);
    bodyLayout->setSpacing(8);
    annotationToolbar_ = buildAnnotationToolbar();
    bodyLayout->addWidget(annotationToolbar_);

    bodySplitter_ = new QSplitter(Qt::Horizontal);
    bodySplitter_->setObjectName("bodySplitter");
    bodySplitter_->setHandleWidth(4);
    bodySplitter_->setChildrenCollapsible(false);
    bodySplitter_->addWidget(buildChartWorkspace());
    rightSidebar_ = buildRightSidebar();
    bodySplitter_->addWidget(rightSidebar_);
    bodySplitter_->setStretchFactor(0, 1);
    bodySplitter_->setStretchFactor(1, 0);
    bodyLayout->addWidget(bodySplitter_, 1);

    mainSplitter_->addWidget(body);
    mainSplitter_->addWidget(buildLogPanel());
    mainSplitter_->setStretchFactor(0, 1);
    mainSplitter_->setStretchFactor(1, 0);
    layout->addWidget(mainSplitter_, 1);

    buildIndicatorDialog();
    buildSettingsPopover();
    buildStatusBar();

    QTimer::singleShot(0, this, [this] {
      bodySplitter_->setSizes({1000, 300});
      mainSplitter_->setSizes({760, 220});
    });
  }

  void buildTopBar() {
    titleBar_ = new QFrame;
    titleBar_->setObjectName("topBar");
    titleBar_->installEventFilter(this);
    auto *bar = new QHBoxLayout(titleBar_);
    bar->setContentsMargins(12, 0, 10, 0);
    bar->setSpacing(12);

    auto *titleIcon = new QLabel;
    titleIcon->setObjectName("titleIcon");
    titleIcon->setPixmap(QIcon(":/app-icon.svg").pixmap(28, 28));
    titleIcon->setFixedSize(30, 30);
    auto *titleCopy = new QWidget;
    titleCopy->setObjectName("barBlock");
    auto *titleCopyLayout = new QHBoxLayout(titleCopy);
    titleCopyLayout->setContentsMargins(0, 0, 0, 0);
    titleCopyLayout->setSpacing(8);
    auto *titleText = new QLabel("AlgoHub");
    titleText->setObjectName("titleText");
    auto *titleSubText = new QLabel("量化复盘终端");
    titleSubText->setObjectName("titleSubText");
    auto *versionText = new QLabel(QString("v%1").arg(QString::fromUtf8(Q4J_APP_VERSION)));
    versionText->setObjectName("titleVersion");
    titleCopyLayout->addWidget(titleText);
    titleCopyLayout->addWidget(titleSubText);
    titleCopyLayout->addWidget(versionText);

    symbol_ = new QLineEdit("XAUUSD");
    symbol_->setObjectName("symbolInput");
    symbol_->setPlaceholderText("搜索 Symbol / 例：BTCUSDT, AAPL");
    symbol_->setFixedWidth(232);
    symbol_->setClearButtonEnabled(true);

    summarySymbol_ = new QLabel("XAUUSD");
    summarySymbol_->setObjectName("summarySymbol");
    priceLabel_ = new QLabel("--");
    priceLabel_->setObjectName("summaryPrice");
    changeLabel_ = new QLabel("--");
    changeLabel_->setObjectName("summaryChange");
    auto *summaryBlock = new QWidget;
    summaryBlock->setObjectName("barBlock");
    auto *summaryLayout = new QHBoxLayout(summaryBlock);
    summaryLayout->setContentsMargins(0, 0, 0, 0);
    summaryLayout->setSpacing(10);
    summaryLayout->addWidget(summarySymbol_);
    summaryLayout->addWidget(priceLabel_);
    summaryLayout->addWidget(changeLabel_);

    high24_ = new QLabel("24h最高 --");
    low24_ = new QLabel("24h最低 --");
    vol24_ = new QLabel("24h成交量 --");
    high24_->setObjectName("summaryStat");
    low24_->setObjectName("summaryStat");
    vol24_->setObjectName("summaryStat");
    auto *statsBlock = new QWidget;
    statsBlock->setObjectName("barBlock");
    auto *statsLayout = new QVBoxLayout(statsBlock);
    statsLayout->setContentsMargins(0, 4, 0, 4);
    statsLayout->setSpacing(1);
    auto *statsTop = new QHBoxLayout;
    statsTop->setContentsMargins(0, 0, 0, 0);
    statsTop->setSpacing(14);
    statsTop->addWidget(high24_);
    statsTop->addWidget(low24_);
    statsLayout->addLayout(statsTop);
    statsLayout->addWidget(vol24_);

    refresh_ = new QPushButton;
    refresh_->setObjectName("topIconButton");
    refresh_->setFixedSize(34, 34);
    refresh_->setIconSize(QSize(18, 18));
    refresh_->setToolTip("刷新行情");
    registerIcon(refresh_, "refresh", 18);
    notifyButton_ = new QPushButton;
    notifyButton_->setObjectName("topIconButton");
    notifyButton_->setFixedSize(34, 34);
    notifyButton_->setIconSize(QSize(18, 18));
    notifyButton_->setToolTip("通知（占位）");
    registerIcon(notifyButton_, "bell", 18);
    settingsGear_ = new QPushButton;
    settingsGear_->setObjectName("topIconButton");
    settingsGear_->setFixedSize(34, 34);
    settingsGear_->setIconSize(QSize(18, 18));
    settingsGear_->setToolTip("设置");
    registerIcon(settingsGear_, "settings", 18);

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
    bar->addWidget(close_);
    bar->addWidget(minimize_);
    bar->addWidget(maximize_);
    bar->addSpacing(8);
    bar->addWidget(titleIcon);
    bar->addWidget(titleCopy);
    bar->addSpacing(6);
    bar->addWidget(symbol_);
    bar->addSpacing(6);
    bar->addWidget(summaryBlock);
    bar->addWidget(statsBlock);
    bar->addStretch(1);
    bar->addWidget(refresh_);
    bar->addWidget(notifyButton_);
    bar->addWidget(settingsGear_);
#else
    minimize_ = new QPushButton;
    maximize_ = new QPushButton;
    close_ = new QPushButton;
    minimize_->setObjectName("windowButton");
    maximize_->setObjectName("windowButton");
    close_->setObjectName("closeButton");
    minimize_->setFixedSize(30, 24);
    maximize_->setFixedSize(30, 24);
    close_->setFixedSize(34, 24);
    minimize_->setIconSize(QSize(15, 15));
    maximize_->setIconSize(QSize(15, 15));
    close_->setIconSize(QSize(15, 15));
    registerIcon(minimize_, "minus", 15);
    registerIcon(close_, "close", 15);
    bar->addWidget(titleIcon);
    bar->addWidget(titleCopy);
    bar->addSpacing(6);
    bar->addWidget(symbol_);
    bar->addSpacing(6);
    bar->addWidget(summaryBlock);
    bar->addWidget(statsBlock);
    bar->addStretch(1);
    bar->addWidget(refresh_);
    bar->addWidget(notifyButton_);
    bar->addWidget(settingsGear_);
    bar->addSpacing(4);
    bar->addWidget(minimize_);
    bar->addWidget(maximize_);
    bar->addWidget(close_);
#endif
  }

  // ---- Bottom status bar ------------------------------------------------------
  void buildStatusBar() {
    statusBar_ = statusBar();
    statusBar_->setSizeGripEnabled(false);
    sbConnection_ = new QLabel("○ 连接中");
    sbConnection_->setObjectName("sbDisconnected");
    sbBackend_ = new QLabel("后端 --");
    sbBackend_->setObjectName("sbItem");
    sbRealtime_ = new QLabel("实时 --");
    sbRealtime_->setObjectName("sbItem");
    sbLatency_ = new QLabel("延迟 --");
    sbLatency_->setObjectName("sbItem");
    statusBar_->addWidget(sbConnection_);
    statusBar_->addPermanentWidget(sbBackend_);
    statusBar_->addPermanentWidget(sbRealtime_);
    statusBar_->addPermanentWidget(sbLatency_);
  }

  void setConnectionStatus(const QString &text, bool live) {
    if (sbConnection_) {
      sbConnection_->setText((live ? "● " : "○ ") + text);
      const QString col = live ? Theme::green() : (dark_ ? Theme::textMuted() : Theme::lTextMuted());
      sbConnection_->setStyleSheet(QString("color:%1; padding:0 8px; font-weight:%2;").arg(col, live ? "600" : "400"));
    }
    if (infoSource_) infoSource_->setText(QString("数据源：%1 %2").arg(live ? "●" : "○", text));
  }

  void updateStatusBar() {
    if (!sbBackend_) return;
    if (client_.hasConfiguredBackend()) {
      sbBackend_->setText(QString("后端 %1").arg(client_.backendBase()));
      sbRealtime_->setText(client_.realtimeEnabled() ? "实时 开启" : "实时 关闭");
    } else {
      sbBackend_->setText("后端 未配置");
      sbRealtime_->setText("实时 --");
    }
  }

  // ---- Chart workspace: toolbar + legend + chart + subcharts + replay bar ----
  QWidget *buildChartWorkspace() {
    auto *workspace = new QWidget;
    workspace->setObjectName("chartWorkspace");
    auto *layout = new QVBoxLayout(workspace);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    chart_ = new ChartWidget;
    chart_->installEventFilter(this);

    layout->addWidget(buildChartToolbar());

    auto *legend = new QFrame;
    legend->setObjectName("chartLegend");
    auto *legendLayout = new QHBoxLayout(legend);
    legendLayout->setContentsMargins(12, 4, 12, 4);
    legendLayout->setSpacing(16);
    legendSymbol_ = new QLabel("XAUUSD · 15 · Binance");
    legendSymbol_->setObjectName("legendSymbol");
    ohlc_ = new QLabel("开 --  高 --  低 --  收 --");
    ohlc_->setObjectName("legendOhlc");
    events_ = new QLabel("事件 --");
    events_->setObjectName("legendMuted");
    range_ = new QLabel("可视范围 --");
    range_->setObjectName("legendMuted");
    legendLayout->addWidget(legendSymbol_);
    legendLayout->addWidget(ohlc_);
    legendLayout->addStretch(1);
    legendLayout->addWidget(events_);
    legendLayout->addWidget(range_);
    layout->addWidget(legend);

    chartSplitter_ = new QSplitter(Qt::Vertical);
    chartSplitter_->setObjectName("chartSplitter");
    chartSplitter_->setHandleWidth(4);
    chartSplitter_->setChildrenCollapsible(false);
    chartSplitter_->addWidget(chart_);

    subchartContainer_ = new QWidget;
    subchartContainer_->setObjectName("subchartContainer");
    subchartLayout_ = new QVBoxLayout(subchartContainer_);
    subchartLayout_->setContentsMargins(0, 0, 0, 0);
    subchartLayout_->setSpacing(4);
    subchartContainer_->setVisible(false);
    chartSplitter_->addWidget(subchartContainer_);
    chartSplitter_->setStretchFactor(0, 1);
    chartSplitter_->setStretchFactor(1, 0);
    layout->addWidget(chartSplitter_, 1);

    layout->addWidget(buildReplayBar());
    return workspace;
  }

  void addTimeframeButton(QHBoxLayout *layout, const QString &label, const QString &token) {
    auto *button = new QPushButton(label);
    button->setObjectName("tfButton");
    button->setCheckable(true);
    button->setProperty("tf", token);
    button->setFixedHeight(30);
    button->setMinimumWidth(38);
    timeframeGroup_->addButton(button);
    timeframeButtons_.insert(token, button);
    if (token == currentTimeframe_) button->setChecked(true);
    connect(button, &QPushButton::clicked, this, [this, token] { selectTimeframe(token); });
    layout->addWidget(button);
  }

  QFrame *buildChartToolbar() {
    auto *toolbar = new QFrame;
    toolbar->setObjectName("chartToolbar");
    auto *layout = new QHBoxLayout(toolbar);
    layout->setContentsMargins(8, 5, 8, 5);
    layout->setSpacing(6);

    timeframeGroup_ = new QButtonGroup(this);
    timeframeGroup_->setExclusive(true);
    addTimeframeButton(layout, "1m", "1M");
    addTimeframeButton(layout, "5m", "5M");
    addTimeframeButton(layout, "15m", "15M");
    addTimeframeButton(layout, "1h", "1H");
    addTimeframeButton(layout, "4h", "4H");
    addTimeframeButton(layout, "1D", "1D");

    moreTimeframe_ = new QPushButton("更多 ▾");
    moreTimeframe_->setObjectName("tfButton");
    moreTimeframe_->setFixedHeight(30);
    buildMoreTimeframeMenu();
    layout->addWidget(moreTimeframe_);

    auto *sep1 = new QFrame;
    sep1->setObjectName("toolbarVSep");
    sep1->setFixedSize(1, 22);
    layout->addWidget(sep1);

    indicators_ = new QPushButton("指标 ▾");
    indicators_->setObjectName("toolButton");
    indicators_->setIconSize(QSize(16, 16));
    registerIcon(indicators_, "indicator", 16);
    buildIndicatorsMenu();
    layout->addWidget(indicators_);

    customIndicators_ = new QPushButton("自定义指标 ▾");
    customIndicators_->setObjectName("toolButton");
    customIndicators_->setIconSize(QSize(16, 16));
    registerIcon(customIndicators_, "function", 16);
    layout->addWidget(customIndicators_);
    connect(customIndicators_, &QPushButton::clicked, this, [this] {
      rebuildCustomIndicatorList();
      updateIndicatorErrorText();
      indicatorDialog_->show();
      indicatorDialog_->raise();
    });

    layout->addStretch(1);

    replayToggle_ = new QPushButton("K线回放");
    replayToggle_->setObjectName("toolButton");
    replayToggle_->setCheckable(true);
    replayToggle_->setIconSize(QSize(16, 16));
    registerIcon(replayToggle_, "history", 16);
    layout->addWidget(replayToggle_);

    rightToggleBtn_ = new QPushButton;
    rightToggleBtn_->setObjectName("collapseButton");
    rightToggleBtn_->setFixedSize(28, 30);
    rightToggleBtn_->setIconSize(QSize(16, 16));
    rightToggleBtn_->setToolTip("显示 / 隐藏策略侧边栏");
    registerIcon(rightToggleBtn_, "chevron-right", 16);
    layout->addWidget(rightToggleBtn_);
    connect(rightToggleBtn_, &QPushButton::clicked, this, [this] {
      setRightSidebarCollapsed(rightSidebar_->isVisible());
    });
    return toolbar;
  }

  void buildMoreTimeframeMenu() {
    auto *menu = new QMenu(moreTimeframe_);
    auto addGroup = [&](const QString &title, const QVector<QPair<QString, QString>> &items) {
      auto *header = menu->addAction(title);
      header->setEnabled(false);
      for (const auto &item : items) {
        QAction *act = menu->addAction(item.first);
        const QString token = item.second;
        connect(act, &QAction::triggered, this, [this, token] { selectTimeframe(token); });
      }
      menu->addSeparator();
    };
    addGroup("秒级", {{"1s", "1S"}, {"5s", "5S"}, {"15s", "15S"}, {"30s", "30S"}});
    addGroup("分钟", {{"1m", "1M"}, {"3m", "3M"}, {"5m", "5M"}, {"15m", "15M"}, {"30m", "30M"}});
    addGroup("小时", {{"1h", "1H"}, {"2h", "2H"}, {"4h", "4H"}, {"6h", "6H"}, {"12h", "12H"}});
    auto *dayHeader = menu->addAction("日线及以上");
    dayHeader->setEnabled(false);
    for (const auto &item : QVector<QPair<QString, QString>>{{"1D", "1D"}, {"1W", "1W"}, {"1M线", "1MO"}}) {
      QAction *act = menu->addAction(item.first);
      const QString token = item.second;
      connect(act, &QAction::triggered, this, [this, token] { selectTimeframe(token); });
    }
    moreTimeframe_->setMenu(menu);
  }

  void buildIndicatorsMenu() {
    auto *menu = new QMenu(indicators_);
    auto addSubchartAction = [&](const QString &label, const QString &key) {
      QAction *act = menu->addAction(label);
      act->setCheckable(true);
      indicatorActions_.insert(key, act);
      connect(act, &QAction::toggled, this, [this, key](bool on) {
        if (on) addSubchart(key);
        else removeSubchart(key);
      });
    };
    auto addMainPlaceholder = [&](const QString &label) {
      QAction *act = menu->addAction(label);
      act->setCheckable(true);
      connect(act, &QAction::toggled, this, [this, label](bool on) {
        appendDebugLog(QString("INFO 指标 %1 %2（主图叠加待接入数据）").arg(label, on ? "已开启" : "已关闭"));
      });
    };
    auto *overlayHeader = menu->addAction("主图叠加");
    overlayHeader->setEnabled(false);
    addMainPlaceholder("MA 均线");
    addMainPlaceholder("EMA");
    addMainPlaceholder("BOLL 布林带");
    fvgMenuAction_ = menu->addAction("FVG Circle");
    fvgMenuAction_->setCheckable(true);
    fvgMenuAction_->setChecked(chart_->fvgCircleSettings().enabled);
    connect(fvgMenuAction_, &QAction::toggled, this, [this](bool on) {
      const FvgCircleSettings s = chart_->fvgCircleSettings();
      chart_->setFvgCircleSettings(on, s.leftRightBars, s.minGapTicks);
      saveIndicatorSettings();
    });
    menu->addSeparator();
    auto *subHeader = menu->addAction("副图指标");
    subHeader->setEnabled(false);
    addSubchartAction("VOL 成交量", "VOL");
    addSubchartAction("MACD", "MACD");
    addSubchartAction("RSI", "RSI");
    addSubchartAction("KDJ", "KDJ");
    indicators_->setMenu(menu);
    indicatorMenu_ = menu;
  }

  // ---- Subchart placeholder panes (closable) ---------------------------------
  void addSubchart(const QString &key) {
    if (subcharts_.contains(key)) return;
    auto *pane = new QFrame;
    pane->setObjectName("subchartPane");
    pane->setMinimumHeight(96);
    auto *paneLayout = new QVBoxLayout(pane);
    paneLayout->setContentsMargins(0, 0, 0, 0);
    paneLayout->setSpacing(0);
    auto *headerRow = new QFrame;
    headerRow->setObjectName("subchartHeader");
    auto *headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(10, 3, 6, 3);
    headerLayout->setSpacing(8);
    auto *title = new QLabel(key);
    title->setObjectName("subchartTitle");
    auto *closeBtn = new QPushButton("×");
    closeBtn->setObjectName("subchartClose");
    closeBtn->setFixedSize(22, 22);
    closeBtn->setToolTip("关闭副图");
    headerLayout->addWidget(title);
    headerLayout->addStretch(1);
    headerLayout->addWidget(closeBtn);
    auto *placeholder = new QLabel("暂未接入指标数据");
    placeholder->setObjectName("subchartPlaceholder");
    placeholder->setAlignment(Qt::AlignCenter);
    paneLayout->addWidget(headerRow);
    paneLayout->addWidget(placeholder, 1);
    subchartLayout_->addWidget(pane);
    subcharts_.insert(key, pane);
    subchartContainer_->setVisible(true);
    connect(closeBtn, &QPushButton::clicked, this, [this, key] {
      if (QAction *act = indicatorActions_.value(key, nullptr)) {
        act->setChecked(false);  // triggers removeSubchart
      } else {
        removeSubchart(key);
      }
    });
  }

  void removeSubchart(const QString &key) {
    if (auto *pane = subcharts_.take(key)) pane->deleteLater();
    if (subcharts_.isEmpty()) subchartContainer_->setVisible(false);
  }

  // ---- Replay control bar (shown only while replay is active) ----------------
  QFrame *buildReplayBar() {
    auto *bar = new QFrame;
    bar->setObjectName("replayBar");
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(12, 7, 12, 7);
    layout->setSpacing(8);

    auto *title = new QLabel("K线回放");
    title->setObjectName("replayTitle");
    layout->addWidget(title);

    auto *calIcon = new QLabel;
    calIcon->setPixmap(tintedPixmap("calendar", dark_ ? Theme::cTextSecondary() : QColor(Theme::lTextSecondary()), 16));
    layout->addWidget(calIcon);
    replayTime_ = new QDateTimeEdit;
    replayTime_->setObjectName("replayTime");
    replayTime_->setDisplayFormat("yyyy-MM-dd HH:mm");
    replayTime_->setCalendarPopup(true);
    replayTime_->setTimeSpec(Qt::LocalTime);
    replayTime_->setMinimumDateTime(QDateTime(QDate(2000, 1, 1), QTime(0, 0)));
    replayTime_->setMaximumDateTime(QDateTime(QDate(2099, 12, 31), QTime(23, 59)));
    replayTime_->setDateTime(normalizeReplayMinute(QDateTime::currentDateTime()));
    replayTime_->setToolTip("回放起始时间，精度到分钟");
    replayTime_->setFixedWidth(168);
    replayTime_->setFixedHeight(30);
    layout->addWidget(replayTime_);

    auto *sep = new QFrame;
    sep->setObjectName("toolbarVSep");
    sep->setFixedSize(1, 20);
    layout->addWidget(sep);

    replayHome_ = new QPushButton;
    replayPrev_ = new QPushButton;
    replayPlay_ = new QPushButton;
    replayStep_ = new QPushButton;
    replayEnd_ = new QPushButton;
    for (QPushButton *b : {replayHome_, replayPrev_, replayPlay_, replayStep_, replayEnd_}) {
      b->setObjectName("replayButton");
      b->setFixedSize(34, 30);
      b->setIconSize(QSize(16, 16));
    }
    replayPlay_->setCheckable(true);
    replayHome_->setToolTip("回到起点");
    replayPrev_->setToolTip("上一根");
    replayPlay_->setToolTip("播放 / 暂停");
    replayStep_->setToolTip("下一根");
    replayEnd_->setToolTip("到当前");
    registerIcon(replayHome_, "skip-back", 16);
    registerIcon(replayPrev_, "chevron-left", 16);
    registerIcon(replayPlay_, "play", 16);
    registerIcon(replayStep_, "chevron-right", 16);
    registerIcon(replayEnd_, "skip-forward", 16);
    layout->addWidget(replayHome_);
    layout->addWidget(replayPrev_);
    layout->addWidget(replayPlay_);
    layout->addWidget(replayStep_);
    layout->addWidget(replayEnd_);

    replaySpeed_ = new QPushButton("10x ▾");
    replaySpeed_->setObjectName("toolButton");
    replaySpeed_->setFixedHeight(30);
    auto *speedMenu = new QMenu(replaySpeed_);
    for (int s : {1, 2, 5, 10, 20, 50}) {
      QAction *act = speedMenu->addAction(QString("%1x").arg(s));
      connect(act, &QAction::triggered, this, [this, s] { setReplaySpeed(s); });
    }
    replaySpeed_->setMenu(speedMenu);
    layout->addWidget(replaySpeed_);

    replayStartLabel_ = new QLabel("起点 --");
    replayStartLabel_->setObjectName("replayInfo");
    layout->addWidget(replayStartLabel_);

    replaySlider_ = new QSlider(Qt::Horizontal);
    replaySlider_->setObjectName("replaySlider");
    replaySlider_->setRange(0, 1000);
    layout->addWidget(replaySlider_, 1);

    replayCurrentLabel_ = new QLabel("当前 --");
    replayCurrentLabel_->setObjectName("replayInfo");
    layout->addWidget(replayCurrentLabel_);

    replayBar_ = bar;
    replayBar_->setVisible(false);
    return bar;
  }

  // ---- Right strategy sidebar -------------------------------------------------
  QWidget *buildRightSidebar() {
    auto *sidebar = new QWidget;
    sidebar->setObjectName("rightSidebar");
    sidebar->setMinimumWidth(280);
    sidebar->setMaximumWidth(360);
    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    rightTabs_ = new QTabWidget;
    rightTabs_->setObjectName("rightTabs");
    rightTabs_->addTab(buildStrategyTab(), "策略");
    rightTabs_->addTab(buildCustomServerTab(), "自定义服务端");
    layout->addWidget(rightTabs_, 1);
    return sidebar;
  }

  QWidget *buildPlaceholderTab(const QString &text) {
    auto *page = new QWidget;
    page->setObjectName("tabPage");
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 16, 12, 16);
    auto *label = new QLabel(text);
    label->setObjectName("mutedLabel");
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignCenter);
    layout->addStretch(1);
    layout->addWidget(label);
    layout->addStretch(1);
    return page;
  }

  QWidget *buildStrategyTab() {
    auto *page = new QWidget;
    page->setObjectName("tabPage");
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel("服务端策略列表（API）");
    title->setObjectName("sectionLabel");
    refreshStrategyBtn_ = new QPushButton("刷新");
    refreshStrategyBtn_->setObjectName("toolButton");
    refreshStrategyBtn_->setFixedHeight(28);
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    titleRow->addWidget(refreshStrategyBtn_);
    layout->addLayout(titleRow);
    connect(refreshStrategyBtn_, &QPushButton::clicked, this, [this] {
      if (client_.hasConfiguredBackend()) client_.loadOverlayStrategies();
    });

    strategyTable_ = new QTableWidget(0, 3);
    strategyTable_->setObjectName("strategyTable");
    strategyTable_->setHorizontalHeaderLabels({"策略名称", "趋势类型", "更新时间"});
    strategyTable_->verticalHeader()->setVisible(false);
    strategyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    strategyTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    strategyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    strategyTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    strategyTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    strategyTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    strategyTable_->setMinimumHeight(160);
    layout->addWidget(strategyTable_, 1);
    connect(strategyTable_, &QTableWidget::itemSelectionChanged, this, [this] {
      const int row = strategyTable_->currentRow();
      if (row < 0) return;
      if (auto *item = strategyTable_->item(row, 0)) {
        setSelectedStrategyName(item->text());
        updateLoadStrategyButton();
      }
    });
    connect(strategyTable_, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
      loadStrategy();
    });

    loadStrategyBtn_ = new QPushButton("请选择策略");
    loadStrategyBtn_->setObjectName("strategyLoadButton");
    loadStrategyBtn_->setFixedHeight(36);
    layout->addWidget(loadStrategyBtn_);
    connect(loadStrategyBtn_, &QPushButton::clicked, this, [this] { loadStrategy(); });

    auto *infoBox = new QFrame;
    infoBox->setObjectName("infoCard");
    auto *infoLayout = new QVBoxLayout(infoBox);
    infoLayout->setContentsMargins(12, 10, 12, 10);
    infoLayout->setSpacing(6);
    auto *infoTitle = new QLabel("当前策略信息");
    infoTitle->setObjectName("sectionLabel");
    infoStart_ = new QLabel("数据起点：--");
    infoCurrent_ = new QLabel("事件数量：--");
    infoSpeed_ = new QLabel("回放速度：10x");
    infoSource_ = new QLabel("数据源：○ 未连接");
    infoLatency_ = new QLabel("延迟：--");
    for (QLabel *l : {infoStart_, infoCurrent_, infoSpeed_, infoSource_, infoLatency_}) l->setObjectName("infoLine");
    infoLayout->addWidget(infoTitle);
    infoLayout->addWidget(infoStart_);
    infoLayout->addWidget(infoCurrent_);
    infoLayout->addWidget(infoSpeed_);
    infoLayout->addWidget(infoSource_);
    infoLayout->addWidget(infoLatency_);
    layout->addWidget(infoBox);
    return page;
  }

  QWidget *buildCustomServerTab() {
    auto *page = new QWidget;
    page->setObjectName("tabPage");
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 14, 12, 14);
    layout->setSpacing(10);
    auto *title = new QLabel("自定义服务端");
    title->setObjectName("sectionLabel");
    layout->addWidget(title);

    serverInfoLabel_ = new QLabel;
    serverInfoLabel_->setObjectName("mutedLabel");
    serverInfoLabel_->setWordWrap(true);
    layout->addWidget(serverInfoLabel_);

    auto *form = new QFrame;
    form->setObjectName("formCard");
    auto *formLayout = new QVBoxLayout(form);
    formLayout->setContentsMargins(12, 12, 12, 12);
    formLayout->setSpacing(8);

    auto addField = [&](const QString &label, QWidget *field) {
      auto *l = new QLabel(label);
      l->setObjectName("fieldLabel");
      formLayout->addWidget(l);
      formLayout->addWidget(field);
    };

    backendUrl_ = new QLineEdit;
    backendUrl_->setPlaceholderText("http://127.0.0.1:8080");
    addField("HTTP 接口地址", backendUrl_);
    wsUrl_ = new QLineEdit;
    wsUrl_->setPlaceholderText("ws://127.0.0.1:8080/ws");
    addField("WebSocket 地址", wsUrl_);
    realtime_ = new QCheckBox("启用实时推送");
    formLayout->addWidget(realtime_);

    auto *saveBtn = new QPushButton("保存并连接");
    saveBtn->setObjectName("strategyLoadButton");
    saveBtn->setFixedHeight(34);
    formLayout->addWidget(saveBtn);
    connect(saveBtn, &QPushButton::clicked, this, [this] { saveBackendFromForm(); });
    layout->addWidget(form);
    layout->addStretch(1);

    syncServerForm();
    updateServerInfoLabel();
    return page;
  }

  void syncServerForm() {
    if (backendUrl_) backendUrl_->setText(client_.backendBase());
    if (wsUrl_) wsUrl_->setText(client_.wsBase());
    if (realtime_) realtime_->setChecked(client_.realtimeEnabled());
  }

  void saveBackendFromForm() {
    const QString backend = backendUrl_ ? backendUrl_->text().trimmed() : QString();
    if (backend.isEmpty()) {
      QMessageBox::warning(this, "服务端配置", "HTTP 后端地址不能为空。");
      return;
    }
    client_.configureBackend(backend, wsUrl_ ? wsUrl_->text() : QString(), realtime_ && realtime_->isChecked());
    saveBackendSettings();
    updateServerInfoLabel();
    updateStatusBar();
    client_.loadOverlayStrategies();
    refresh();
  }

  // First launch / disconnected: switch to the config tab instead of a modal.
  void promptConfigureBackend() {
    if (rightTabs_) {
      if (rightSidebar_ && !rightSidebar_->isVisible()) setRightSidebarCollapsed(false);
      rightTabs_->setCurrentIndex(rightTabs_->count() - 1);
    }
    if (serverInfoLabel_) serverInfoLabel_->setText("请在下方填写服务端地址并保存以开始使用。");
  }

  void updateServerInfoLabel() {
    if (!serverInfoLabel_) return;
    if (!client_.hasConfiguredBackend()) {
      serverInfoLabel_->setText("服务端未连接，请在下方填写地址并保存。");
      return;
    }
    serverInfoLabel_->setText(QString("当前已连接：%1").arg(client_.backendBase()));
  }

  // ---- Bottom server log panel ------------------------------------------------
  QFrame *buildLogPanel() {
    logPanel_ = new QFrame;
    logPanel_->setObjectName("logPanel");
    auto *layout = new QVBoxLayout(logPanel_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QFrame;
    header->setObjectName("logHeader");
    header->setFixedHeight(32);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 0, 10, 0);
    headerLayout->setSpacing(8);
    logCollapseBtn_ = new QPushButton;
    logCollapseBtn_->setObjectName("collapseButton");
    logCollapseBtn_->setFixedSize(24, 22);
    logCollapseBtn_->setIconSize(QSize(14, 14));
    logCollapseBtn_->setToolTip("展开 / 折叠服务日志");
    registerIcon(logCollapseBtn_, "chevron-down", 14);
    auto *title = new QLabel("服务日志");
    title->setObjectName("sectionLabel");
    logLevel_ = new QComboBox;
    logLevel_->setObjectName("logLevel");
    logLevel_->addItems({"全部", "INFO", "WARN", "ERROR", "DEBUG"});
    logLevel_->setFixedWidth(96);
    logClear_ = new QPushButton("清空");
    logExport_ = new QPushButton("导出");
    logClear_->setObjectName("toolButton");
    logExport_->setObjectName("toolButton");
    logClear_->setFixedHeight(26);
    logExport_->setFixedHeight(26);
    logClear_->setIconSize(QSize(14, 14));
    logExport_->setIconSize(QSize(14, 14));
    registerIcon(logClear_, "trash", 14);
    registerIcon(logExport_, "download", 14);
    statInfo_ = new QLabel("INFO 0");
    statWarn_ = new QLabel("WARN 0");
    statError_ = new QLabel("ERROR 0");
    statDebug_ = new QLabel("DEBUG 0");
    statInfo_->setObjectName("statInfo");
    statWarn_->setObjectName("statWarn");
    statError_->setObjectName("statError");
    statDebug_->setObjectName("statDebug");
    headerLayout->addWidget(logCollapseBtn_);
    headerLayout->addWidget(title);
    headerLayout->addSpacing(8);
    headerLayout->addWidget(logLevel_);
    headerLayout->addStretch(1);
    headerLayout->addWidget(statInfo_);
    headerLayout->addWidget(statWarn_);
    headerLayout->addWidget(statError_);
    headerLayout->addWidget(statDebug_);
    headerLayout->addSpacing(10);
    headerLayout->addWidget(logClear_);
    headerLayout->addWidget(logExport_);
    layout->addWidget(header);

    logBody_ = new QWidget;
    auto *bodyLayout = new QVBoxLayout(logBody_);
    bodyLayout->setContentsMargins(8, 6, 8, 8);
    bodyLayout->setSpacing(0);
    logTable_ = new QTableWidget(0, 4);
    logTable_->setObjectName("logTable");
    logTable_->setHorizontalHeaderLabels({"时间", "级别", "模块", "信息"});
    logTable_->verticalHeader()->setVisible(false);
    logTable_->setShowGrid(false);
    logTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    logTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    logTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    logTable_->setWordWrap(false);
    logTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    logTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    logTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    logTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    logTable_->verticalHeader()->setDefaultSectionSize(22);
    bodyLayout->addWidget(logTable_, 1);
    layout->addWidget(logBody_, 1);

    connect(logCollapseBtn_, &QPushButton::clicked, this, [this] {
      setLogCollapsed(logBody_->isVisible());
    });
    connect(logClear_, &QPushButton::clicked, this, [this] {
      logEntries_.clear();
      logInfo_ = logWarn_ = logError_ = logDebug_ = 0;
      renderLogView();
      updateLogStats();
    });
    connect(logExport_, &QPushButton::clicked, this, &MainWindow::exportLogs);
    connect(logLevel_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { renderLogView(); });
    return logPanel_;
  }

  void buildSettingsPopover() {
    settingsPopover_ = new QFrame(this, Qt::Popup);
    settingsPopover_->setObjectName("settingsPopover");
    settingsPopover_->setMinimumWidth(280);
    auto *layout = new QVBoxLayout(settingsPopover_);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);
    auto *title = new QLabel("设置");
    title->setObjectName("dialogTitle");
    layout->addWidget(title);

    auto addRow = [&](const QString &label, QWidget *control) {
      auto *row = new QHBoxLayout;
      row->setContentsMargins(0, 0, 0, 0);
      auto *l = new QLabel(label);
      row->addWidget(l);
      row->addStretch(1);
      row->addWidget(control);
      layout->addLayout(row);
    };

    updateButton_ = new QPushButton(QString("检查更新  v%1").arg(QString::fromUtf8(Q4J_APP_VERSION)));
    updateButton_->setObjectName("toolButton");
    addRow("检查更新", updateButton_);

    timeZone_ = new QComboBox;
    timeZone_->setObjectName("settingsCombo");
    timeZone_->setFixedWidth(168);
    timeZone_->setToolTip("X轴时间时区");
    populateTimeZones();
    addRow("时区设置", timeZone_);

    themeToggle_ = new QCheckBox;
    themeToggle_->setChecked(dark_);
    addRow("深色模式", themeToggle_);
    connect(themeToggle_, &QCheckBox::toggled, this, [this](bool on) {
      dark_ = on;
      applyTheme();
    });

    autoSaveLayout_ = new QCheckBox;
    autoSaveLayout_->setChecked(true);
    addRow("自动保存布局", autoSaveLayout_);

    auto *aboutBtn = new QPushButton("关于 AlgoHub");
    aboutBtn->setObjectName("toolButton");
    layout->addWidget(aboutBtn);
    connect(aboutBtn, &QPushButton::clicked, this, [this] {
      QMessageBox::information(this, "关于 AlgoHub",
        QString("AlgoHub 量化复盘终端\n版本 %1").arg(QString::fromUtf8(Q4J_APP_VERSION)));
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
    auto *builtinGroup = new QGroupBox("内置指标");
    auto *builtinLayout = new QFormLayout(builtinGroup);
    builtinLayout->setContentsMargins(12, 12, 12, 12);
    builtinLayout->setSpacing(8);
    fvgCircleEnabled_ = new QCheckBox("显示 FVG Circle");
    fvgCircleEnabled_->setChecked(chart_->fvgCircleSettings().enabled);
    fvgCircleN_ = new QSpinBox;
    fvgCircleN_->setRange(1, 50);
    fvgCircleN_->setValue(chart_->fvgCircleSettings().leftRightBars);
    fvgCircleN_->setMinimumHeight(30);
    fvgCircleMinGapTicks_ = new QSpinBox;
    fvgCircleMinGapTicks_->setRange(0, 10000);
    fvgCircleMinGapTicks_->setValue(chart_->fvgCircleSettings().minGapTicks);
    fvgCircleMinGapTicks_->setMinimumHeight(30);
    builtinLayout->addRow("FVG Circle", fvgCircleEnabled_);
    builtinLayout->addRow("Left / Right N", fvgCircleN_);
    builtinLayout->addRow("Min gap ticks", fvgCircleMinGapTicks_);
    indicatorErrorText_ = new QPlainTextEdit;
    indicatorErrorText_->setObjectName("errorBox");
    indicatorErrorText_->setReadOnly(true);
    indicatorErrorText_->setMinimumHeight(160);
    auto *errorLabel = new QLabel("脚本错误");
    errorLabel->setObjectName("sectionLabel");
    auto *hint = new QLabel("指标参数显示在左侧列表中。图表左上角 Layers 也可以直接开关每个指标。");
    hint->setWordWrap(true);
    hint->setObjectName("mutedLabel");
    sideLayout->addWidget(builtinGroup);
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
    auto saveBuiltinIndicatorSettings = [this] {
      if (!fvgCircleEnabled_ || !fvgCircleN_ || !fvgCircleMinGapTicks_) return;
      chart_->setFvgCircleSettings(fvgCircleEnabled_->isChecked(), fvgCircleN_->value(), fvgCircleMinGapTicks_->value());
      saveIndicatorSettings();
      updateIndicatorErrorText();
    };
    connect(fvgCircleEnabled_, &QCheckBox::toggled, this, saveBuiltinIndicatorSettings);
    connect(fvgCircleN_, QOverload<int>::of(&QSpinBox::valueChanged), this, saveBuiltinIndicatorSettings);
    connect(fvgCircleMinGapTicks_, QOverload<int>::of(&QSpinBox::valueChanged), this, saveBuiltinIndicatorSettings);
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
    populateStrategyTable(values, current);
  }

  void populateStrategyTable(const QStringList &names, const QString &selected) {
    if (!strategyTable_) return;
    QSignalBlocker blocker(strategyTable_);
    strategyTable_->setRowCount(names.size());
    int selectedRow = -1;
    for (int i = 0; i < names.size(); ++i) {
      auto *nameItem = new QTableWidgetItem(names[i]);
      auto *typeItem = new QTableWidgetItem("趋势跟随");
      auto *timeItem = new QTableWidgetItem("--");
      strategyTable_->setItem(i, 0, nameItem);
      strategyTable_->setItem(i, 1, typeItem);
      strategyTable_->setItem(i, 2, timeItem);
      if (names[i].compare(selected, Qt::CaseInsensitive) == 0) selectedRow = i;
    }
    if (selectedRow >= 0) strategyTable_->selectRow(selectedRow);
    updateLoadStrategyButton();
  }

  void updateLoadStrategyButton() {
    if (!loadStrategyBtn_) return;
    const bool hasSelection = strategyTable_ && strategyTable_->currentRow() >= 0;
    loadStrategyBtn_->setEnabled(hasSelection);
    loadStrategyBtn_->setText(hasSelection ? "加载策略" : "请选择策略");
  }

  // Load the selected strategy's overlay events. The chart renders them
  // naturally via overlayEventsLoaded; replay remains an independent feature.
  void loadStrategy() {
    if (strategyTable_) {
      const int row = strategyTable_->currentRow();
      if (row >= 0) {
        if (auto *item = strategyTable_->item(row, 0)) setSelectedStrategyName(item->text());
      }
    }
    saveStrategySettings();
    loadStrategyBtn_->setText("正在加载策略事件…");
    refresh();
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
      range_->setText("可视范围 --");
      return;
    }
    const QString start = formatDisplayTime(lastRangeStartMs_, "MM-dd HH:mm");
    const QString end = formatDisplayTime(lastRangeEndMs_, "MM-dd HH:mm");
    range_->setText(QString("可视范围 %1 → %2").arg(start, end));
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
    connect(settingsGear_, &QPushButton::clicked, this, &MainWindow::openSettingsPopover);
    connect(chart_, &ChartWidget::annotationToolChanged, this, [this](ChartWidget::AnnotationTool tool) {
      syncDrawingTool(tool);
    });
    connect(chart_, &ChartWidget::fvgCircleVisibilityChanged, this, [this](bool enabled) {
      if (fvgCircleEnabled_ && fvgCircleEnabled_->isChecked() != enabled) fvgCircleEnabled_->setChecked(enabled);
      if (fvgMenuAction_ && fvgMenuAction_->isChecked() != enabled) {
        QSignalBlocker blocker(fvgMenuAction_);
        fvgMenuAction_->setChecked(enabled);
      }
      saveIndicatorSettings();
    });
    connect(chart_, &ChartWidget::customIndicatorVisibilityChanged, this, [this](const QString &, bool) {
      saveIndicatorSettings();
      rebuildCustomIndicatorList();
      updateIndicatorErrorText();
    });
    connect(magnetButton_, &QToolButton::toggled, chart_, &ChartWidget::setMagnetEnabled);
    connect(updateButton_, &QPushButton::clicked, this, &MainWindow::checkForUpdates);
    replayTimer_.setInterval(replayIntervalForSpeed(replaySpeedValue_));
    connect(&replayTimer_, &QTimer::timeout, this, &MainWindow::stepReplay);
    connect(replayToggle_, &QPushButton::toggled, this, [this](bool enabled) {
      replayActive_ = enabled;
      if (replayBar_) replayBar_->setVisible(enabled);
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
      updateReplayUi();
    });
    connect(replayStep_, &QPushButton::clicked, this, [this] {
      if (!replayActive_ && replayToggle_) replayToggle_->setChecked(true);
      stepReplay();
    });
    connect(replayHome_, &QPushButton::clicked, this, [this] { replayJumpToStart(); });
    connect(replayPrev_, &QPushButton::clicked, this, [this] { stepReplayBackward(); });
    connect(replayEnd_, &QPushButton::clicked, this, [this] { replayJumpToEnd(); });
    connect(replaySlider_, &QSlider::sliderMoved, this, [this](int value) { seekReplay(value); });
    connect(timeZone_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
      applyTimeZoneSetting();
      saveDisplaySettings();
    });
    connect(&client_, &CandleClient::candlesLoaded, this, [this](const QVector<Candle> &candles) {
      loadedCandles_ = candles;
      normalizeLoadedCandles();
      if (loadedCandles_.isEmpty()) {
        chart_->showLoadError("没有K线数据");
        updateIndicatorErrorText();
        return;
      }
      syncReplayBounds();
      applyReplayView();
      updateMarketSummary();
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
      events_->setText(QString("事件 %1").arg(count));
      loadedEventCount_ = count;
      if (infoCurrent_) infoCurrent_->setText(QString("事件数量：%1").arg(count));
      updateLoadStrategyButton();
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
      updateMarketSummary();
      updateIndicatorErrorText();
    });
    connect(&client_, &CandleClient::debugLog, this, &MainWindow::appendDebugLog);
    connect(&client_, &CandleClient::errorMessage, this, [this](const QString &message) {
      if (message.trimmed().isEmpty()) chart_->clearMessage();
      else chart_->showMessage(message.trimmed());
    });
    connect(&client_, &CandleClient::loadFailed, this, [this](const QString &message) {
      chart_->showLoadError(message.trimmed().isEmpty() ? "加载失败" : message.trimmed());
      events_->setText("事件 --");
      range_->setText("可视范围 --");
      ohlc_->setText("开 --  高 --  低 --  收 --");
      priceLabel_->setText("--");
      changeLabel_->setText("--");
    });
    connect(chart_, &ChartWidget::olderCandlesRequested, &client_, &CandleClient::loadOlder);
    connect(chart_, &ChartWidget::overlayRangeChanged, &client_, &CandleClient::loadOverlayRange);
    connect(chart_, &ChartWidget::visibleRangeChanged, this, [this](qint64 startMs, qint64 endMs, int firstIndex, int lastIndex) {
      lastRangeStartMs_ = startMs;
      lastRangeEndMs_ = endMs;
      lastRangeFirstIndex_ = firstIndex;
      lastRangeLastIndex_ = lastIndex;
      if (startMs <= 0 || endMs <= 0 || firstIndex < 0 || lastIndex < 0) {
        range_->setText("可视范围 --");
        return;
      }
      updateVisibleRangeLabel();
    });
    connect(&client_, &CandleClient::statusChanged, this, [this](const QString &status, bool live) {
      setConnectionStatus(status, live);
      updateServerInfoLabel();
      updateStatusBar();
    });
    connect(chart_, &ChartWidget::hoveredCandleChanged, this, [this](const Candle *c) {
      if (!c) {
        ohlc_->setText("开 --  高 --  低 --  收 --");
        return;
      }
      const QColor col = c->close >= c->open ? Theme::cGreen() : Theme::cRed();
      ohlc_->setText(QString("<span style='color:%5'>开 %1  高 %2  低 %3  收 %4</span>")
        .arg(c->open).arg(c->high).arg(c->low).arg(c->close).arg(col.name()));
    });
  }

  void openSettingsPopover() {
    if (!settingsPopover_) return;
    settingsPopover_->adjustSize();
    const QPoint below = settingsGear_->mapToGlobal(QPoint(settingsGear_->width() - settingsPopover_->sizeHint().width(), settingsGear_->height() + 6));
    settingsPopover_->move(below);
    settingsPopover_->show();
  }

  void updateMarketSummary() {
    if (loadedCandles_.isEmpty()) return;
    const Candle &last = loadedCandles_.last();
    const Candle &first = loadedCandles_.first();
    const double change = last.close - first.open;
    const double pct = first.open != 0 ? change / first.open * 100.0 : 0.0;
    const QColor col = change >= 0 ? Theme::cGreen() : Theme::cRed();
    priceLabel_->setText(QString::number(last.close, 'f', 2));
    priceLabel_->setStyleSheet(QString("color:%1;").arg(col.name()));
    changeLabel_->setText(QString("%1%2 (%3%4%)").arg(change >= 0 ? "+" : "").arg(change, 0, 'f', 2)
      .arg(pct >= 0 ? "+" : "").arg(pct, 0, 'f', 2));
    changeLabel_->setStyleSheet(QString("color:%1;").arg(col.name()));
  }

  void applyTheme() {
    chart_->setDark(dark_);
    if (themeToggle_ && themeToggle_->isChecked() != dark_) {
      QSignalBlocker blocker(themeToggle_);
      themeToggle_->setChecked(dark_);
    }
    qApp->setStyleSheet(appStyleSheet(dark_));
    refreshIcons();
#ifndef Q_OS_MACOS
    if (maximize_) {
      maximize_->setIconSize(QSize(15, 15));
      maximize_->setIcon(themedIcon((maximizedAnimated_ || isMaximized()) ? "restore" : "square", 15));
    }
#endif
    updateStatusBar();
  }

  // Single source of truth for the application stylesheet. Palette tokens are
  // OKLCH-derived (see theme.hpp) and substituted into one template so dark and
  // light modes stay structurally identical.
  QString appStyleSheet(bool dark) const {
    QHash<QString, QString> t;
    if (dark) {
      t = {
        {"BG_APP", Theme::bgApp()}, {"BG_PANEL", Theme::bgPanel()}, {"BG_PANEL2", Theme::bgPanel2()},
        {"BG_TOOLBAR", Theme::bgToolbar()}, {"BG_ELEV", Theme::bgElevated()}, {"BG_HOVER", Theme::bgHover()},
        {"BORDER", Theme::borderSubtle()}, {"BORDER_STRONG", Theme::borderStrong()},
        {"TXT", Theme::textPrimary()}, {"TXT2", Theme::textSecondary()}, {"TXT3", Theme::textMuted()},
        {"BRAND", Theme::brandBlue()}, {"BRAND_HOVER", Theme::brandBlueHover()}, {"BRAND_SOFT", Theme::brandBlueSoft()},
        {"GREEN", Theme::green()}, {"RED", Theme::red()},
        {"HEADER_BG", Theme::bgElevated()}, {"SHELL_BORDER", "rgba(120, 170, 255, 0.22)"},
      };
    } else {
      t = {
        {"BG_APP", Theme::lBgApp()}, {"BG_PANEL", Theme::lBgPanel()}, {"BG_PANEL2", Theme::lBgPanel()},
        {"BG_TOOLBAR", Theme::lBgElevated()}, {"BG_ELEV", Theme::lBgElevated()}, {"BG_HOVER", Theme::lBgHover()},
        {"BORDER", Theme::lBorderSubtle()}, {"BORDER_STRONG", Theme::lBorderStrong()},
        {"TXT", Theme::lTextPrimary()}, {"TXT2", Theme::lTextSecondary()}, {"TXT3", Theme::lTextMuted()},
        {"BRAND", Theme::lBrandBlue()}, {"BRAND_HOVER", "#1D4ED8"}, {"BRAND_SOFT", "rgba(37, 99, 235, 0.12)"},
        {"GREEN", "#0E9F6E"}, {"RED", "#E02D3C"},
        {"HEADER_BG", "#FFFFFF"}, {"SHELL_BORDER", "rgba(30, 50, 80, 0.22)"},
      };
    }
    QString css = QStringLiteral(R"CSS(
      * { font-family: "Inter", "Segoe UI", "PingFang SC", "Microsoft YaHei", "Noto Sans CJK SC", sans-serif; }
      QWidget { background: @BG_APP@; color: @TXT@; font-size: 13px; }
      QMainWindow { background: @BG_APP@; }
      QWidget#appShell { background: @BG_APP@; border: 1px solid @SHELL_BORDER@; }
      QToolTip { background: @BG_ELEV@; color: @TXT@; border: 1px solid @BORDER@; padding: 4px 8px; border-radius: 5px; }

      /* ---- Top navigation bar ---- */
      QFrame#topBar {
        min-height: 60px; max-height: 60px;
        background: @HEADER_BG@;
        border: 0; border-bottom: 1px solid @BORDER_STRONG@;
      }
      QWidget#barBlock { background: transparent; }
      QLabel#titleIcon { background: transparent; }
      QLabel#titleText { background: transparent; color: @TXT@; font-size: 17px; font-weight: 700; }
      QLabel#titleSubText { background: transparent; color: @TXT2@; font-size: 12px; }
      QLabel#titleVersion { background: transparent; color: @TXT3@; font-size: 11px; }
      QLabel#summarySymbol { background: transparent; color: @TXT@; font-size: 15px; font-weight: 600; }
      QLabel#summaryPrice { background: transparent; color: @TXT@; font-size: 17px; font-weight: 700; }
      QLabel#summaryChange { background: transparent; color: @TXT2@; font-size: 13px; font-weight: 600; }
      QLabel#summaryStat { background: transparent; color: @TXT2@; font-size: 11px; }

      QLineEdit#symbolInput {
        min-height: 36px; max-height: 36px;
        background: @BG_APP@; border: 1px solid @BORDER@; border-radius: 8px;
        padding: 0 12px; color: @TXT@;
        selection-background-color: @BRAND@; selection-color: #ffffff;
      }
      QLineEdit#symbolInput:focus { border-color: @BRAND@; }

      QPushButton#topIconButton {
        background: transparent; border: 1px solid @BORDER@; border-radius: 8px; color: @TXT2@;
      }
      QPushButton#topIconButton:hover { background: @BG_HOVER@; border-color: @BORDER_STRONG@; }
      QPushButton#windowButton, QPushButton#closeButton {
        min-height: 24px; max-height: 24px; background: transparent;
        border: 1px solid transparent; border-radius: 6px; color: @TXT2@;
      }
      QPushButton#windowButton:hover { background: @BG_HOVER@; }
      QPushButton#closeButton:hover { background: @RED@; }

      /* ---- Splitters / body ---- */
      QSplitter::handle { background: transparent; }
      QSplitter::handle:hover { background: @BORDER_STRONG@; }
      QWidget#bodyArea, QWidget#chartWorkspace { background: @BG_APP@; }

      /* ---- Annotation toolbar ---- */
      QFrame#annotationToolbar { background: @BG_TOOLBAR@; border: 1px solid @BORDER@; border-radius: 10px; }
      QFrame#toolGroup { background: transparent; }
      QToolButton#annotationToolButton, QPushButton#annotationToolButton {
        background: transparent; border: 1px solid transparent; border-radius: 8px; color: @TXT2@;
      }
      QToolButton#annotationToolButton:hover, QPushButton#annotationToolButton:hover { background: @BG_HOVER@; }
      QToolButton#annotationToolButton:checked, QPushButton#annotationToolButton:checked {
        background: @BRAND_SOFT@; border-color: @BRAND@;
      }
      QToolButton#annotationToolButton:disabled, QPushButton#annotationToolButton:disabled { color: @TXT3@; }
      QToolButton#annotationToolButton::menu-indicator { image: none; width: 0; }
      QPushButton#groupChevron {
        background: transparent; border: none; color: @TXT3@;
      }
      QPushButton#groupChevron:hover { color: @TXT@; }
      QFrame#annotationDivider, QFrame#toolbarVSep { background: @BORDER@; border: 0; }
      QFrame#toolPopover { background: @BG_ELEV@; border: 1px solid @BORDER_STRONG@; border-radius: 10px; }

      /* ---- Chart toolbar ---- */
      QFrame#chartToolbar { background: @BG_PANEL@; border: 1px solid @BORDER@; border-radius: 10px; }
      QPushButton#tfButton {
        background: transparent; border: 1px solid transparent; border-radius: 7px; color: @TXT2@;
        padding: 0 10px; font-weight: 600;
      }
      QPushButton#tfButton:hover { background: @BG_HOVER@; color: @TXT@; }
      QPushButton#tfButton:checked { background: @BRAND_SOFT@; color: @BRAND_HOVER@; border-color: @BRAND@; }
      QPushButton#toolButton {
        background: @BG_ELEV@; border: 1px solid @BORDER@; border-radius: 7px; color: @TXT@;
        padding: 5px 12px; font-weight: 500;
      }
      QPushButton#toolButton:hover { background: @BG_HOVER@; border-color: @BORDER_STRONG@; }
      QPushButton#toolButton:checked { background: @BRAND@; color: #ffffff; border-color: @BRAND@; }
      QPushButton#toolButton::menu-indicator { image: none; width: 0; }
      QPushButton#collapseButton {
        background: transparent; border: 1px solid @BORDER@; border-radius: 7px; color: @TXT2@;
      }
      QPushButton#collapseButton:hover { background: @BG_HOVER@; color: @TXT@; }

      /* ---- Legend ---- */
      QFrame#chartLegend { background: transparent; }
      QLabel#legendSymbol { color: @TXT@; font-weight: 600; font-size: 13px; }
      QLabel#legendOhlc { color: @TXT2@; font-family: "JetBrains Mono", "Consolas", monospace; font-size: 12px; }
      QLabel#legendMuted { color: @TXT3@; font-size: 12px; }

      /* ---- Subcharts ---- */
      QFrame#subchartPane { background: @BG_PANEL@; border: 1px solid @BORDER@; border-radius: 8px; }
      QFrame#subchartHeader { background: transparent; }
      QLabel#subchartTitle { color: @TXT2@; font-weight: 600; }
      QLabel#subchartPlaceholder { color: @TXT3@; }
      QPushButton#subchartClose { background: transparent; border: none; border-radius: 5px; color: @TXT3@; }
      QPushButton#subchartClose:hover { background: @RED@; color: #ffffff; }

      /* ---- Replay bar ---- */
      QFrame#replayBar { background: @BG_PANEL@; border: 1px solid @BORDER@; border-radius: 10px; }
      QLabel#replayTitle { color: @TXT@; font-weight: 600; }
      QLabel#replayInfo { color: @TXT2@; font-family: "JetBrains Mono", "Consolas", monospace; font-size: 12px; }
      QPushButton#replayButton {
        background: @BG_ELEV@; border: 1px solid @BORDER@; border-radius: 7px; color: @TXT@;
      }
      QPushButton#replayButton:hover { background: @BG_HOVER@; border-color: @BORDER_STRONG@; }
      QPushButton#replayButton:checked { background: @BRAND@; border-color: @BRAND@; }
      QSlider#replaySlider::groove:horizontal { height: 4px; background: @BORDER@; border-radius: 2px; }
      QSlider#replaySlider::sub-page:horizontal { background: @BRAND@; border-radius: 2px; }
      QSlider#replaySlider::handle:horizontal {
        width: 14px; height: 14px; margin: -6px 0; border-radius: 7px; background: @BRAND_HOVER@;
      }

      /* ---- Right sidebar / tabs ---- */
      QWidget#rightSidebar { background: @BG_PANEL@; border: 1px solid @BORDER@; border-radius: 10px; }
      QTabWidget#rightTabs::pane { border: 0; background: transparent; }
      QWidget#tabPage { background: transparent; }
      QTabBar::tab {
        background: transparent; color: @TXT3@; padding: 9px 14px; font-weight: 600;
        border: 0; border-bottom: 2px solid transparent;
      }
      QTabBar::tab:hover { color: @TXT2@; }
      QTabBar::tab:selected { color: @TXT@; border-bottom: 2px solid @BRAND@; }

      QLabel#sectionLabel { color: @TXT@; font-weight: 700; font-size: 13px; }
      QLabel#mutedLabel { color: @TXT3@; font-size: 12px; }
      QLabel#fieldLabel { color: @TXT2@; font-size: 12px; }

      QTableWidget#strategyTable, QTableWidget#logTable {
        background: @BG_APP@; border: 1px solid @BORDER@; border-radius: 8px;
        gridline-color: @BORDER@; color: @TXT@; selection-background-color: @BRAND_SOFT@;
        selection-color: @TXT@;
      }
      QTableWidget#strategyTable::item, QTableWidget#logTable::item { padding: 4px 6px; }
      QHeaderView::section {
        background: @BG_ELEV@; color: @TXT2@; padding: 6px 8px; border: 0;
        border-right: 1px solid @BORDER@; border-bottom: 1px solid @BORDER@; font-weight: 600;
      }
      QTableCornerButton::section { background: @BG_ELEV@; border: 0; }

      QPushButton#strategyLoadButton {
        background: @BRAND@; color: #ffffff; border: 0; border-radius: 8px; font-weight: 700; font-size: 13px;
      }
      QPushButton#strategyLoadButton:hover { background: @BRAND_HOVER@; }
      QPushButton#strategyLoadButton:disabled { background: @BG_ELEV@; color: @TXT3@; }

      QFrame#infoCard, QFrame#formCard { background: @BG_APP@; border: 1px solid @BORDER@; border-radius: 8px; }
      QLabel#infoLine { color: @TXT2@; font-size: 12px; }

      /* ---- Inputs ---- */
      QLineEdit, QComboBox, QSpinBox, QDateTimeEdit, QPlainTextEdit {
        background: @BG_APP@; border: 1px solid @BORDER@; border-radius: 7px; padding: 5px 9px; color: @TXT@;
        selection-background-color: @BRAND@; selection-color: #ffffff;
      }
      QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDateTimeEdit:focus { border-color: @BRAND@; }
      QComboBox::drop-down, QDateTimeEdit::drop-down { width: 22px; border: 0; background: transparent; }
      QComboBox::down-arrow, QDateTimeEdit::down-arrow {
        image: none; border-left: 4px solid transparent; border-right: 4px solid transparent;
        border-top: 5px solid @TXT3@; width: 0; height: 0; margin-right: 6px;
      }
      QComboBox QAbstractItemView {
        background: @BG_ELEV@; border: 1px solid @BORDER_STRONG@; border-radius: 8px;
        color: @TXT@; selection-background-color: @BRAND_SOFT@; selection-color: @TXT@; outline: 0;
      }
      QSpinBox::up-button, QSpinBox::down-button { width: 16px; background: @BG_ELEV@; border: 0; }
      QComboBox#logLevel { min-height: 26px; }

      /* ---- Menus ---- */
      QMenu { background: @BG_ELEV@; border: 1px solid @BORDER_STRONG@; border-radius: 8px; padding: 5px; }
      QMenu::item { padding: 6px 22px; border-radius: 6px; color: @TXT@; }
      QMenu::item:selected { background: @BRAND_SOFT@; }
      QMenu::item:disabled { color: @TXT3@; }
      QMenu::separator { height: 1px; background: @BORDER@; margin: 4px 8px; }

      /* ---- Checkboxes ---- */
      QCheckBox { color: @TXT@; spacing: 8px; }
      QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid @BORDER_STRONG@; border-radius: 5px; background: @BG_APP@; }
      QCheckBox::indicator:checked { background: @BRAND@; border-color: @BRAND@; }

      /* ---- Scrollbars ---- */
      QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
      QScrollBar::handle:vertical { background: @BORDER_STRONG@; border-radius: 5px; min-height: 28px; }
      QScrollBar::handle:vertical:hover { background: @BRAND@; }
      QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
      QScrollBar::handle:horizontal { background: @BORDER_STRONG@; border-radius: 5px; min-width: 28px; }
      QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
      QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

      /* ---- Log panel ---- */
      QFrame#logPanel { background: @BG_PANEL@; border-top: 1px solid @BORDER@; }
      QFrame#logHeader { background: @BG_TOOLBAR@; border-bottom: 1px solid @BORDER@; }
      QLabel#statInfo { color: @TXT2@; font-size: 11px; }
      QLabel#statWarn { color: @ORANGE_PLACEHOLDER@; font-size: 11px; }
      QLabel#statError { color: @RED@; font-size: 11px; }
      QLabel#statDebug { color: @TXT3@; font-size: 11px; }

      /* ---- Dialogs / popover ---- */
      QFrame#settingsPopover { background: @BG_ELEV@; border: 1px solid @BORDER_STRONG@; border-radius: 12px; }
      QDialog { background: @BG_PANEL@; }
      QLabel#dialogTitle { color: @TXT@; font-size: 16px; font-weight: 700; }
      QGroupBox {
        background: @BG_APP@; border: 1px solid @BORDER@; border-radius: 8px;
        margin-top: 10px; padding: 10px 12px 12px 12px; color: @TXT2@; font-weight: 600;
      }
      QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }
      QWidget#customIndicatorList { background: transparent; }
      QFrame#indicatorCard { background: @BG_APP@; border: 1px solid @BORDER@; border-radius: 8px; }
      QPlainTextEdit#errorBox {
        background: @BG_APP@; border: 1px solid @BORDER@; border-radius: 8px; color: @RED@;
        font-family: "JetBrains Mono", "Consolas", monospace; font-size: 12px;
      }
      QDialogButtonBox QPushButton {
        background: @BG_ELEV@; border: 1px solid @BORDER@; border-radius: 7px; color: @TXT@;
        padding: 6px 18px; min-height: 26px;
      }
      QDialogButtonBox QPushButton:hover { background: @BG_HOVER@; border-color: @BORDER_STRONG@; }

      /* ---- Status bar ---- */
      QStatusBar { background: @HEADER_BG@; border-top: 1px solid @BORDER@; color: @TXT2@; }
      QStatusBar::item { border: 0; }
      QStatusBar QLabel { color: @TXT2@; font-size: 12px; }
      QLabel#sbItem { color: @TXT2@; padding: 0 8px; }
      QLabel#sbConnected { color: @GREEN@; padding: 0 8px; font-weight: 600; }
      QLabel#sbDisconnected { color: @TXT3@; padding: 0 8px; }
    )CSS");
    t["ORANGE_PLACEHOLDER"] = Theme::orange();
    for (auto it = t.constBegin(); it != t.constEnd(); ++it) {
      css.replace("@" + it.key() + "@", it.value());
    }
    return css;
  }

  QString newDarkThemeCss() const {
    return R"(
      QWidget { background: #07121F; color: #DDE8F5; }
      QMainWindow { background: #07121F; }
      QWidget#appShell { background: #07121F; border: 1px solid rgba(120, 180, 255, 60); }

      QFrame#topBar {
        min-height: 54px; max-height: 54px;
        background: #081421;
        border: 0;
        border-bottom: 1px solid rgba(120, 160, 200, 41);
      }
      QWidget#barBlock { background: transparent; }
      QLabel#titleIcon { background: transparent; }
      QLabel#titleText { background: transparent; color: #DDE8F5; font-size: 17px; font-weight: 700; }
      QLabel#titleSubText { background: transparent; color: #8EA4BC; font-size: 12px; }
      QLabel#titleVersion { background: transparent; color: #5F7488; font-size: 11px; }
      QLabel#summarySymbol { background: transparent; color: #DDE8F5; font-size: 15px; font-weight: 600; }
      QLabel#summaryPrice { background: transparent; color: #DDE8F5; font-size: 17px; font-weight: 700; }
      QLabel#summaryChange { background: transparent; color: #8EA4BC; font-size: 13px; font-weight: 600; }
      QLabel#summaryStat { background: transparent; color: #8EA4BC; font-size: 11px; }
      QLabel#status { background: transparent; color: #8EA4BC; font-size: 13px; qproperty-alignment: AlignCenter; }

      QLineEdit#symbolInput {
        min-height: 34px; max-height: 34px;
        background: #0B1826;
        border: 1px solid rgba(120, 160, 200, 41);
        border-radius: 6px;
        padding: 0 12px;
        color: #DDE8F5;
        selection-background-color: #1677FF;
        selection-color: #ffffff;
      }
      QLineEdit#symbolInput:focus { border-color: rgba(120, 180, 255, 120); }
      QComboBox#exchangeSelect, QComboBox#settingsCombo, QComboBox#logLevel {
        min-height: 34px; max-height: 34px;
        background: #0B1826;
        border: 1px solid rgba(120, 160, 200, 41);
        border-radius: 6px;
        padding: 0 26px 0 12px;
        color: #DDE8F5;
      }
      QComboBox#logLevel { min-height: 26px; max-height: 26px; }
      QComboBox#exchangeSelect::drop-down, QComboBox#settingsCombo::drop-down, QComboBox#logLevel::drop-down {
        width: 22px; border: 0; background: transparent;
      }
      QComboBox#exchangeSelect::down-arrow, QComboBox#settingsCombo::down-arrow, QComboBox#logLevel::down-arrow {
        image: none; border-left: 4px solid transparent; border-right: 4px solid transparent;
        border-top: 5px solid #8EA4BC; width: 0; height: 0;
      }

      QPushButton#topIconButton {
        background: transparent;
        border: 1px solid rgba(120, 160, 200, 41);
        border-radius: 6px;
        color: #8EA4BC;
        font-size: 15px;
      }
      QPushButton#topIconButton:hover { background: #0E2032; border-color: rgba(120, 180, 255, 120); color: #DDE8F5; }
      QPushButton#windowButton, QPushButton#closeButton {
        min-height: 24px; max-height: 24px;
        background: transparent; border: 1px solid transparent; border-radius: 6px;
        color: #8EA4BC; font-size: 16px;
      }
      QPushButton#windowButton:hover { background: #0E2032; color: #DDE8F5; }
      QPushButton#closeButton:hover { background: #EF5350; color: #ffffff; }

      QSplitter#mainSplitter::handle, QSplitter#bodySplitter::handle, QSplitter#chartSplitter::handle {
        background: transparent;
      }
      QSplitter::handle:hover { background: rgba(120, 180, 255, 60); }
      QWidget#bodyArea { background: #07121F; }
      QWidget#chartWorkspace { background: #07121F; }

      QFrame#annotationToolbar {
        background: #0B1826;
        border: 1px solid rgba(120, 160, 200, 41);
        border-radius: 8px;
      }
      QFrame#annotationDivider { background: rgba(120, 160, 200, 50); border: 0; }
      QPushButton#annotationToolButton {
        min-width: 40px; max-width: 40px; min-height: 40px; max-height: 40px;
        background: transparent;
        border: 1px solid transparent;
        border-radius: 7px;
        color: #8EA4BC;
      }
      QPushButton#annotationToolButton:hover { background: #0E2032; border-color: rgba(120, 180, 255, 90); }
      QPushButton#annotationToolButton:checked { background: rgba(22, 119, 255, 46); border-color: #1677FF; }
      QPushButton#annotationToolButton:disabled { color: rgba(143, 164, 188, 90); }

      QFrame#chartToolbar {
        min-height: 42px;
        background: #0B1826;
        border: 1px solid rgba(120, 160, 200, 41);
        border-radius: 8px;
      }
      QFrame#toolbarVSep { background: rgba(120, 160, 200, 50); }
      QPushButton#tfButton {
        background: transparent;
        border: 1px solid transparent;
        border-radius: 5px;
        padding: 0 8px;
        color: #8EA4BC;
        font-size: 12px;
        font-weight: 600;
      }
      QPushButton#tfButton:hover { background: #0E2032; color: #DDE8F5; }
      QPushButton#tfButton:checked { background: rgba(22, 119, 255, 46); border-color: rgba(22, 119, 255, 180); color: #ffffff; }
      QPushButton#tfButton::menu-indicator { image: none; width: 0; }
      QPushButton#toolButton {
        min-height: 30px;
        background: #0E2032;
        border: 1px solid rgba(120, 160, 200, 41);
        border-radius: 6px;
        padding: 0 12px;
        color: #DDE8F5;
        font-size: 13px;
      }
      QPushButton#toolButton:hover { background: #102538; border-color: rgba(120, 180, 255, 120); }
      QPushButton#toolButton::menu-indicator { image: none; width: 0; }
      QPushButton#collapseButton {
        background: #0E2032;
        border: 1px solid rgba(120, 160, 200, 41);
        border-radius: 6px;
        color: #8EA4BC;
        font-size: 15px;
      }
      QPushButton#collapseButton:hover { background: #102538; color: #DDE8F5; }

      QDateTimeEdit#replayTime {
        min-height: 30px; max-height: 30px;
        background: #0B1826;
        border: 1px solid rgba(120, 160, 200, 41);
        border-radius: 6px;
        padding: 0 10px;
        color: #DDE8F5;
        font-size: 12px;
      }
      QDateTimeEdit#replayTime:hover, QDateTimeEdit#replayTime:focus { border-color: rgba(120, 180, 255, 120); }

      QFrame#chartLegend { background: transparent; }
      QLabel#legendSymbol { background: transparent; color: #DDE8F5; font-size: 13px; font-weight: 600; }
      QLabel#legendOhlc { background: transparent; color: #8EA4BC; font-size: 12px; }
      QLabel#legendMuted { background: transparent; color: #5F7488; font-size: 11px; }

      QSplitter#chartSplitter > QWidget { background: #07121F; }
      QWidget#subchartContainer { background: #07121F; }
      QFrame#subchartPane {
        background: #0B1826;
        border: 1px solid rgba(120, 160, 200, 41);
        border-radius: 8px;
      }
      QFrame#subchartHeader { background: transparent; border-bottom: 1px solid rgba(120, 160, 200, 30); }
      QLabel#subchartTitle { background: transparent; color: #DDE8F5; font-size: 12px; font-weight: 600; }
      QLabel#subchartPlaceholder { background: transparent; color: #5F7488; font-size: 12px; }
      QPushButton#subchartClose {
        background: transparent; border: 0; border-radius: 4px; color: #8EA4BC; font-size: 14px;
      }
      QPushButton#subchartClose:hover { background: #EF5350; color: #ffffff; }

      QFrame#replayBar {
        min-height: 44px;
        background: #0B1826;
        border: 1px solid rgba(120, 160, 200, 41);
        border-radius: 8px;
      }
      QLabel#replayTitle { background: transparent; color: #DDE8F5; font-size: 12px; font-weight: 600; }
      QLabel#replayInfo { background: transparent; color: #8EA4BC; font-size: 11px; }
      QPushButton#replayButton {
        background: #0E2032;
        border: 1px solid rgba(120, 160, 200, 41);
        border-radius: 6px;
        color: #DDE8F5;
        font-size: 14px;
      }
      QPushButton#replayButton:hover { background: #102538; border-color: rgba(120, 180, 255, 120); }
      QPushButton#replayButton:checked { background: rgba(22, 119, 255, 46); border-color: #1677FF; color: #ffffff; }
      QSlider#replaySlider::groove:horizontal { height: 4px; background: rgba(120, 160, 200, 50); border-radius: 2px; }
      QSlider#replaySlider::sub-page:horizontal { background: #1677FF; border-radius: 2px; }
      QSlider#replaySlider::handle:horizontal {
        width: 12px; height: 12px; margin: -5px 0; border-radius: 6px; background: #2F8CFF;
      }

      QWidget#rightSidebar { background: #0B1826; border-left: 1px solid rgba(120, 160, 200, 41); }
      QWidget#tabPage { background: #0B1826; }
      QTabWidget#rightTabs::pane { border: 0; background: #0B1826; }
      QTabWidget#rightTabs QTabBar::tab {
        background: transparent;
        color: #8EA4BC;
        padding: 8px 12px;
        border: 0;
        border-bottom: 2px solid transparent;
        font-size: 13px;
      }
      QTabWidget#rightTabs QTabBar::tab:selected { color: #DDE8F5; border-bottom-color: #1677FF; }
      QTabWidget#rightTabs QTabBar::tab:hover { color: #DDE8F5; }

      QLabel#sectionLabel { background: transparent; color: #DDE8F5; font-size: 13px; font-weight: 600; }
      QLabel#mutedLabel { background: transparent; color: #8EA4BC; font-size: 12px; }
      QLabel#infoLine { background: transparent; color: #8EA4BC; font-size: 12px; }
      QFrame#infoCard { background: #0E2032; border: 1px solid rgba(120, 160, 200, 41); border-radius: 8px; }

      QTableWidget#strategyTable {
        background: #0B1826;
        border: 1px solid rgba(120, 160, 200, 41);
        border-radius: 8px;
        gridline-color: transparent;
        color: #DDE8F5;
        font-size: 12px;
        outline: 0;
      }
      QTableWidget#strategyTable::item { padding: 6px 8px; border: 0; }
      QTableWidget#strategyTable::item:selected { background: rgba(22, 119, 255, 46); color: #DDE8F5; }
      QTableWidget#strategyTable QHeaderView::section {
        background: #0E2032;
        color: #8EA4BC;
        border: 0;
        border-bottom: 1px solid rgba(120, 160, 200, 41);
        padding: 6px 8px;
        font-size: 12px;
      }
      QPushButton#strategyLoadButton {
        background: rgba(22, 119, 255, 56);
        border: 1px solid rgba(22, 119, 255, 150);
        border-radius: 6px;
        color: #ffffff;
        font-size: 13px;
        font-weight: 600;
      }
      QPushButton#strategyLoadButton:hover { background: #1677FF; border-color: #2F8CFF; }

      QFrame#logPanel { background: #0B1826; border-top: 1px solid rgba(120, 160, 200, 41); }
      QFrame#logHeader { background: #081421; border-bottom: 1px solid rgba(120, 160, 200, 30); }
      QLabel#statInfo { background: transparent; color: #1677FF; font-size: 11px; font-weight: 600; }
      QLabel#statWarn { background: transparent; color: #F5A623; font-size: 11px; font-weight: 600; }
      QLabel#statError { background: transparent; color: #EF5350; font-size: 11px; font-weight: 600; }
      QLabel#statDebug { background: transparent; color: #A66CFF; font-size: 11px; font-weight: 600; }
      QPlainTextEdit#debugLog {
        background: #07121F;
        border: 0;
        color: #C7D6E8;
        font-family: "Cascadia Mono", "Consolas", "Noto Sans Mono CJK SC", monospace;
        font-size: 12px;
        selection-background-color: #1677FF;
      }

      QFrame#settingsPopover {
        background: #0B1826;
        border: 1px solid rgba(120, 180, 255, 60);
        border-radius: 8px;
      }
      QFrame#settingsPopover QLabel { background: transparent; color: #DDE8F5; font-size: 13px; }
      QLabel#dialogTitle { background: transparent; color: #DDE8F5; font-size: 16px; font-weight: 700; }
    )";
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
#ifndef Q_OS_MACOS
    if (maximize_) maximize_->setIcon(themedIcon(maximizedAnimated_ ? "restore" : "square", 15));
#endif
  }

  QString darkCss() const {
    return R"(
      QWidget { background: #222831; color: #DFD0B8; font-family: "Chiron GoRound TC", "Microsoft YaHei UI", "Segoe UI", "PingFang SC", "Noto Sans CJK SC"; font-size: 14px; font-weight: 400; }
      QMainWindow { background: #222831; }
      QWidget#appShell {
        background: #222831;
        border: 1px solid rgba(70, 86, 112, 150);
      }
      QFrame#titleBar {
        min-height: 48px; max-height: 48px;
        background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #222831, stop:1 #393E46);
        border: 0;
        border-radius: 8px;
      }
      QLabel#titleIcon {
        background: transparent;
      }
      QLabel#titleText {
        background: transparent;
        color: #DFD0B8;
        font-size: 20px;
        font-weight: 600;
      }
      QLabel#titleSubText {
        background: transparent;
        color: #948979;
        font-size: 12px;
        font-weight: 400;
      }
      QPushButton#windowButton, QPushButton#closeButton {
        min-height: 32px; max-height: 32px;
        background: transparent;
        border: 1px solid transparent;
        border-radius: 6px;
        padding: 0;
        color: #948979;
        font-size: 18px;
        font-weight: 400;
      }
      QPushButton#windowButton:hover { background: rgba(148, 137, 121, 24); border-color: rgba(148, 137, 121, 44); color: #DFD0B8; }
      QPushButton#closeButton:hover { background: #ff5c5c; border-color: #ff5c5c; color: #ffffff; }
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
      QFrame#header {
        background: rgba(34, 40, 49, 238);
        border: 1px solid rgba(148, 137, 121, 210);
        border-radius: 8px;
      }
      QFrame#footer {
        min-height: 38px;
        background: rgba(34, 40, 49, 238);
        border: 1px solid rgba(148, 137, 121, 210);
        border-radius: 8px;
      }
      QFrame#toolbarGroup {
        background: rgba(57, 62, 70, 160);
        border: 1px solid rgba(148, 137, 121, 155);
        border-radius: 8px;
      }
      QWidget#chartRow { background: transparent; }
      QFrame#annotationToolbar {
        background: rgba(34, 40, 49, 240);
        border: 1px solid rgba(148, 137, 121, 210);
        border-radius: 8px;
      }
      QFrame#annotationDivider {
        background: rgba(148, 137, 121, 150);
        border: 0;
      }
      QFrame#annotationFloatingToolbar {
        background: rgba(57, 62, 70, 245);
        border: 1px solid rgba(148, 137, 121, 220);
        border-radius: 8px;
      }
      QFrame#annotationFloatingToolbar QSpinBox {
        min-height: 24px; max-height: 24px;
        background: #393E46;
        border: 1px solid rgba(148, 137, 121, 190);
        color: #DFD0B8;
        padding: 0 4px;
      }
      QPushButton#annotationToolButton {
        min-width: 42px; max-width: 42px; min-height: 42px; max-height: 42px;
        background: rgba(57, 62, 70, 155);
        border: 1px solid rgba(148, 137, 121, 155);
        border-radius: 7px;
        padding: 0;
        color: #DFD0B8;
        font-size: 16px;
        font-weight: 400;
      }
      QPushButton#annotationToolButton:hover {
        background: rgba(57, 62, 70, 220);
        border-color: rgba(223, 208, 184, 150);
        color: #DFD0B8;
      }
      QPushButton#annotationToolButton:checked {
        background: rgba(223, 208, 184, 30);
        border-color: #DFD0B8;
        color: #DFD0B8;
      }
      QLineEdit, QComboBox, QPushButton {
        min-height: 36px; max-height: 36px;
        background: #393E46;
        border: 1px solid rgba(148, 137, 121, 190);
        border-radius: 7px;
        padding: 0 12px;
        color: #DFD0B8;
        font-size: 14px;
        font-weight: 500;
      }
      QLineEdit#symbolInput {
        background: #222831;
        border-color: rgba(148, 137, 121, 210);
        padding: 0 14px;
        selection-background-color: #DFD0B8;
        selection-color: #222831;
      }
      QComboBox#intervalInput {
        background: #222831;
        border-color: rgba(148, 137, 121, 210);
        padding: 0 28px 0 14px;
      }
      QComboBox#intervalInput::drop-down {
        width: 26px;
        border: 0;
        background: transparent;
      }
      QComboBox#intervalInput::down-arrow {
        image: none;
        border-left: 5px solid transparent;
        border-right: 5px solid transparent;
        border-top: 6px solid #948979;
        width: 0px;
        height: 0px;
      }
      QPushButton#toolButton {
        background: #393E46;
        border-color: rgba(148, 137, 121, 190);
        color: #DFD0B8;
      }
      QPushButton#toolButton:hover, QComboBox#intervalInput:hover, QLineEdit#symbolInput:hover {
        background: #393E46;
        border-color: rgba(223, 208, 184, 150);
      }
      QLineEdit#symbolInput:focus, QComboBox#intervalInput:focus, QPushButton#toolButton:focus {
        border-color: #DFD0B8;
      }
      QDateTimeEdit#replayTime {
        min-height: 36px; max-height: 36px;
        background: #222831;
        border: 1px solid rgba(148, 137, 121, 210);
        border-radius: 7px;
        padding: 0 12px;
        color: #DFD0B8;
      }
      QDateTimeEdit#replayTime:hover, QDateTimeEdit#replayTime:focus {
        background: #393E46;
        border-color: rgba(223, 208, 184, 150);
      }
      QPushButton#iconButton {
        min-height: 36px; max-height: 36px;
        background: #393E46;
        border: 1px solid rgba(148, 137, 121, 190);
        border-radius: 7px;
        padding: 0;
        color: #DFD0B8;
        font-size: 16px;
        font-weight: 500;
      }
      QPushButton#refreshButton {
        min-height: 36px; max-height: 36px;
        background: rgba(223, 208, 184, 18);
        border: 1px solid rgba(223, 208, 184, 120);
        border-radius: 7px;
        color: #DFD0B8;
        padding: 0 12px;
        font-weight: 600;
      }
      QPushButton#refreshButton:hover { background: rgba(223, 208, 184, 42); border-color: #DFD0B8; }
      QPushButton:hover, QLineEdit:focus, QComboBox:focus { border-color: #DFD0B8; }
      QLabel#status {
        background: transparent;
        color: #DFD0B8;
        font-size: 14px;
        font-weight: 500;
        qproperty-alignment: AlignCenter;
      }
      QFrame#footer QLabel {
        background: transparent;
        color: #DFD0B8;
        font-size: 14px;
        font-weight: 500;
      }
      QComboBox#footerTimeZone {
        min-height: 30px; max-height: 30px;
        background: #393E46;
        border: 1px solid rgba(148, 137, 121, 190);
        border-radius: 7px;
        padding: 0 28px 0 12px;
        color: #DFD0B8;
        font-size: 13px;
      }
      QComboBox#footerTimeZone:hover, QComboBox#footerTimeZone:focus {
        border-color: rgba(223, 208, 184, 150);
      }
      QComboBox#footerTimeZone::drop-down {
        width: 26px;
        border: 0;
        background: transparent;
      }
      QComboBox#footerTimeZone::down-arrow {
        image: none;
        border-left: 5px solid transparent;
        border-right: 5px solid transparent;
        border-top: 6px solid #948979;
        width: 0px;
        height: 0px;
      }
      QDialog QWidget {
        background: transparent;
      }
      QDialog QLabel, QDialog QCheckBox, QDialog QDialogButtonBox {
        background: transparent;
        color: #DFD0B8;
        font-size: 14px;
        font-weight: 400;
      }
      QDialog QLabel#dialogTitle {
        color: #DFD0B8;
        font-size: 20px;
        font-weight: 600;
      }
      QDialog QLabel#sectionLabel {
        color: #DFD0B8;
        font-size: 13px;
        font-weight: 600;
      }
      QDialog QLabel#mutedLabel {
        color: #948979;
        font-size: 12px;
      }
      QWidget#indicatorRow {
        background: rgba(57, 62, 70, 180);
        border: 1px solid rgba(148, 137, 121, 150);
        border-radius: 8px;
      }
      QWidget#indicatorParams {
        background: rgba(57, 62, 70, 120);
        border-left: 1px solid rgba(223, 208, 184, 80);
      }
      QSplitter::handle {
        background: rgba(148, 137, 121, 130);
        width: 1px;
      }
      QDialog QLineEdit, QDialog QComboBox {
        background: #222831;
        border: 1px solid rgba(148, 137, 121, 190);
        color: #DFD0B8;
      }
      QDialog QPlainTextEdit {
        background: #222831;
        border: 1px solid rgba(148, 137, 121, 170);
        color: #DFD0B8;
        font-family: "Cascadia Mono", "Consolas", "Chiron GoRound TC";
      }
      QDialog {
        background: #222831;
        border: 1px solid rgba(148, 137, 121, 180);
      }
    )";
  }

  QString modernControlsCss(bool dark) const {
    if (dark) {
      return R"(
        QToolTip {
          background: #222831;
          color: #DFD0B8;
          border: 1px solid rgba(148, 137, 121, 210);
          padding: 6px 8px;
        }
        QMenu {
          background: #222831;
          color: #DFD0B8;
          border: 1px solid rgba(148, 137, 121, 210);
          border-radius: 8px;
          padding: 8px;
        }
        QMenu::item {
          min-height: 28px;
          padding: 5px 30px 5px 12px;
          border-radius: 6px;
        }
        QMenu::item:selected {
          background: rgba(223, 208, 184, 26);
          color: #DFD0B8;
        }
        QAbstractItemView {
          background: #222831;
          color: #DFD0B8;
          border: 1px solid rgba(148, 137, 121, 210);
          border-radius: 8px;
          outline: 0;
          selection-background-color: rgba(223, 208, 184, 42);
          selection-color: #DFD0B8;
        }
        QAbstractItemView::item {
          min-height: 30px;
          padding: 5px 10px;
        }
        QAbstractItemView::item:hover {
          background: rgba(148, 137, 121, 18);
        }
        QDialog QPushButton, QMessageBox QPushButton {
          min-height: 34px;
          background: #393E46;
          border: 1px solid rgba(148, 137, 121, 190);
          border-radius: 7px;
          padding: 0 16px;
          color: #DFD0B8;
          font-weight: 500;
        }
        QDialog QPushButton:hover, QMessageBox QPushButton:hover {
          background: #393E46;
          border-color: rgba(223, 208, 184, 150);
        }
        QDialogButtonBox QPushButton:default {
          background: #DFD0B8;
          border-color: #DFD0B8;
          color: #222831;
        }
        QGroupBox {
          background: rgba(57, 62, 70, 150);
          border: 1px solid rgba(148, 137, 121, 150);
          border-radius: 8px;
          margin-top: 18px;
          padding: 12px 10px 10px 10px;
          font-weight: 600;
        }
        QGroupBox::title {
          subcontrol-origin: margin;
          subcontrol-position: top left;
          left: 10px;
          padding: 0 6px;
          color: #DFD0B8;
          background: #222831;
        }
        QCheckBox {
          spacing: 8px;
        }
        QCheckBox::indicator {
          width: 16px;
          height: 16px;
          border: 1px solid rgba(148, 137, 121, 125);
          border-radius: 4px;
          background: #222831;
        }
        QCheckBox::indicator:hover {
          border-color: #DFD0B8;
          background: #393E46;
        }
        QCheckBox::indicator:checked {
          background: #DFD0B8;
          border-color: #DFD0B8;
        }
        QCheckBox::indicator:checked:disabled {
          background: rgba(223, 208, 184, 90);
        }
        QSpinBox, QDoubleSpinBox, QDateTimeEdit, QDialog QSpinBox, QDialog QDoubleSpinBox, QDialog QDateTimeEdit {
          min-height: 34px;
          background: #222831;
          border: 1px solid rgba(148, 137, 121, 190);
          border-radius: 7px;
          padding: 0 10px;
          color: #DFD0B8;
          selection-background-color: #DFD0B8;
          selection-color: #222831;
        }
        QSpinBox:focus, QDoubleSpinBox:focus, QDateTimeEdit:focus {
          border-color: #DFD0B8;
        }
        QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
          width: 18px;
          border: 0;
          background: transparent;
        }
        QSpinBox::up-arrow, QSpinBox::down-arrow, QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow {
          width: 0;
          height: 0;
        }
        QScrollBar:vertical {
          background: transparent;
          width: 10px;
          margin: 2px;
        }
        QScrollBar::handle:vertical {
          background: rgba(148, 137, 121, 70);
          border-radius: 4px;
          min-height: 28px;
        }
        QScrollBar::handle:vertical:hover {
          background: rgba(223, 208, 184, 145);
        }
        QScrollBar:horizontal {
          background: transparent;
          height: 10px;
          margin: 2px;
        }
        QScrollBar::handle:horizontal {
          background: rgba(148, 137, 121, 70);
          border-radius: 4px;
          min-width: 28px;
        }
        QScrollBar::handle:horizontal:hover {
          background: rgba(223, 208, 184, 145);
        }
        QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page {
          border: 0;
          background: transparent;
          width: 0;
          height: 0;
        }
        QCalendarWidget QWidget {
          background: #222831;
          color: #DFD0B8;
        }
        QCalendarWidget QToolButton {
          background: transparent;
          border: 1px solid transparent;
          border-radius: 6px;
          color: #DFD0B8;
          padding: 4px 8px;
        }
        QCalendarWidget QToolButton:hover {
          background: rgba(223, 208, 184, 28);
          border-color: rgba(223, 208, 184, 130);
        }
        QCalendarWidget QTableView {
          background: #222831;
          border: 0;
          selection-background-color: #DFD0B8;
          selection-color: #222831;
        }
      )";
    }
    return R"(
      QToolTip {
        background: #ffffff;
        color: #131916;
        border: 1px solid rgba(23, 31, 27, 50);
        padding: 6px 8px;
      }
      QMenu {
        background: #fffdf7;
        color: #131916;
        border: 1px solid rgba(23, 31, 27, 34);
        padding: 6px;
      }
      QMenu::item {
        min-height: 24px;
        padding: 4px 28px 4px 10px;
        border-radius: 3px;
      }
      QMenu::item:selected {
        background: rgba(240, 182, 79, 58);
        color: #8f5f0e;
      }
      QAbstractItemView {
        background: #fffdf7;
        color: #131916;
        border: 1px solid rgba(23, 31, 27, 34);
        outline: 0;
        selection-background-color: rgba(240, 182, 79, 80);
        selection-color: #131916;
      }
      QAbstractItemView::item {
        min-height: 26px;
        padding: 4px 8px;
      }
      QAbstractItemView::item:hover {
        background: rgba(23, 31, 27, 10);
      }
      QDialog QPushButton, QMessageBox QPushButton {
        min-height: 30px;
        background: #f7f8f2;
        border: 1px solid rgba(23, 31, 27, 42);
        border-radius: 4px;
        padding: 0 14px;
        color: #131916;
        font-weight: 500;
      }
      QDialog QPushButton:hover, QMessageBox QPushButton:hover {
        background: #ffffff;
        border-color: rgba(178, 122, 23, 150);
      }
      QDialogButtonBox QPushButton:default {
        background: #f0b64f;
        border-color: #d79b2f;
        color: #111813;
      }
      QGroupBox {
        background: rgba(23, 31, 27, 6);
        border: 1px solid rgba(23, 31, 27, 24);
        border-radius: 5px;
        margin-top: 18px;
        padding: 12px 10px 10px 10px;
        font-weight: 600;
      }
      QGroupBox::title {
        subcontrol-origin: margin;
        subcontrol-position: top left;
        left: 10px;
        padding: 0 6px;
        color: #b27a17;
        background: #fffdf7;
      }
      QCheckBox {
        spacing: 8px;
      }
      QCheckBox::indicator {
        width: 16px;
        height: 16px;
        border: 1px solid rgba(23, 31, 27, 62);
        border-radius: 3px;
        background: #ffffff;
      }
      QCheckBox::indicator:hover {
        border-color: #b27a17;
        background: #fffaf0;
      }
      QCheckBox::indicator:checked {
        background: #f0b64f;
        border-color: #b27a17;
      }
      QCheckBox::indicator:checked:disabled {
        background: rgba(240, 182, 79, 110);
      }
      QSpinBox, QDoubleSpinBox, QDateTimeEdit, QDialog QSpinBox, QDialog QDoubleSpinBox, QDialog QDateTimeEdit {
        min-height: 30px;
        background: #ffffff;
        border: 1px solid rgba(23, 31, 27, 42);
        border-radius: 4px;
        padding: 0 8px;
        color: #131916;
        selection-background-color: #f0b64f;
        selection-color: #111813;
      }
      QSpinBox:focus, QDoubleSpinBox:focus, QDateTimeEdit:focus {
        border-color: #b27a17;
      }
      QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
        width: 18px;
        border: 0;
        background: transparent;
      }
      QSpinBox::up-arrow, QSpinBox::down-arrow, QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow {
        width: 0;
        height: 0;
      }
      QScrollBar:vertical {
        background: transparent;
        width: 10px;
        margin: 2px;
      }
      QScrollBar::handle:vertical {
        background: rgba(23, 31, 27, 48);
        border-radius: 4px;
        min-height: 28px;
      }
      QScrollBar::handle:vertical:hover {
        background: rgba(178, 122, 23, 125);
      }
      QScrollBar:horizontal {
        background: transparent;
        height: 10px;
        margin: 2px;
      }
      QScrollBar::handle:horizontal {
        background: rgba(23, 31, 27, 48);
        border-radius: 4px;
        min-width: 28px;
      }
      QScrollBar::handle:horizontal:hover {
        background: rgba(178, 122, 23, 125);
      }
      QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page {
        border: 0;
        background: transparent;
        width: 0;
        height: 0;
      }
      QCalendarWidget QWidget {
        background: #fffdf7;
        color: #131916;
      }
      QCalendarWidget QToolButton {
        background: transparent;
        border: 1px solid transparent;
        border-radius: 3px;
        color: #131916;
        padding: 4px 8px;
      }
      QCalendarWidget QToolButton:hover {
        background: rgba(240, 182, 79, 58);
        border-color: rgba(178, 122, 23, 120);
      }
      QCalendarWidget QTableView {
        background: #fffdf7;
        border: 0;
        selection-background-color: #f0b64f;
        selection-color: #111813;
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
        border-radius: 6px;
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
        border-radius: 6px;
      }
      QFrame#toolbarGroup {
        background: #f7f8f2;
        border: 1px solid rgba(23, 31, 27, 24);
        border-radius: 6px;
      }
      QWidget#chartRow { background: transparent; }
      QFrame#annotationToolbar {
        background: #fffdf7;
        border: 1px solid rgba(23, 31, 27, 34);
        border-radius: 6px;
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

  struct LogEntry { QString time; QString level; QString module; QString message; };

  QString classifyLogLevel(const QString &message) const {
    const QString lower = message.toLower();
    if (message.contains("ERROR") || lower.contains("error") || message.contains("失败") || message.contains("错误")) return "ERROR";
    if (message.contains("WARN") || lower.contains("warn") || message.contains("超时") || message.contains("重连")) return "WARN";
    if (message.startsWith("Chart upsert") || message.contains("DEBUG")) return "DEBUG";
    return "INFO";
  }

  // Heuristically derive the originating module from the message text.
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

  QColor logLevelColor(const QString &level) const {
    if (level == "ERROR") return dark_ ? Theme::cRed() : QColor("#E02D3C");
    if (level == "WARN") return Theme::cOrange();
    if (level == "DEBUG") return dark_ ? Theme::cTextMuted() : QColor(Theme::lTextMuted());
    return dark_ ? Theme::cGreen() : QColor("#0E9F6E");
  }

  void appendLogRow(const LogEntry &entry) {
    if (!logTable_ || !logLevelMatches(entry.level)) return;
    QScrollBar *bar = logTable_->verticalScrollBar();
    const bool atBottom = !bar || bar->value() >= bar->maximum() - 2;
    const int row = logTable_->rowCount();
    logTable_->insertRow(row);
    auto *timeItem = new QTableWidgetItem(entry.time);
    auto *levelItem = new QTableWidgetItem(entry.level);
    levelItem->setForeground(logLevelColor(entry.level));
    auto *moduleItem = new QTableWidgetItem(entry.module);
    moduleItem->setForeground(dark_ ? Theme::cTextSecondary() : QColor(Theme::lTextSecondary()));
    auto *msgItem = new QTableWidgetItem(entry.message);
    logTable_->setItem(row, 0, timeItem);
    logTable_->setItem(row, 1, levelItem);
    logTable_->setItem(row, 2, moduleItem);
    logTable_->setItem(row, 3, msgItem);
    if (atBottom) logTable_->scrollToBottom();
  }

  void appendDebugLog(const QString &message) {
    LogEntry entry;
    entry.time = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    entry.level = classifyLogLevel(message);
    entry.module = deriveLogModule(message);
    entry.message = message;
    logEntries_.append(entry);
    if (logEntries_.size() > 5000) {
      logEntries_.removeFirst();
      if (logTable_ && logTable_->rowCount() > 0) logTable_->removeRow(0);
    }
    if (entry.level == "ERROR") ++logError_;
    else if (entry.level == "WARN") ++logWarn_;
    else if (entry.level == "DEBUG") ++logDebug_;
    else ++logInfo_;
    updateLogStats();
    appendLogRow(entry);
  }

  bool logLevelMatches(const QString &level) const {
    if (!logLevel_) return true;
    const QString filter = logLevel_->currentText();
    return filter == "全部" || filter == level;
  }

  void renderLogView() {
    if (!logTable_) return;
    logTable_->setRowCount(0);
    for (const LogEntry &entry : logEntries_) appendLogRow(entry);
  }

  void updateLogStats() {
    if (statInfo_) statInfo_->setText(QString("INFO %1").arg(logInfo_));
    if (statWarn_) statWarn_->setText(QString("WARN %1").arg(logWarn_));
    if (statError_) statError_->setText(QString("ERROR %1").arg(logError_));
    if (statDebug_) statDebug_->setText(QString("DEBUG %1").arg(logDebug_));
  }

  void exportLogs() {
    const QString path = QFileDialog::getSaveFileName(this, "导出日志", "service-log.tsv", "TSV Files (*.tsv);;Log Files (*.log);;All Files (*)");
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QMessageBox::warning(this, "导出失败", QString("无法写入：%1").arg(path));
      return;
    }
    QStringList lines;
    lines << "时间\t级别\t模块\t信息";
    for (const LogEntry &e : logEntries_) {
      lines << QString("%1\t%2\t%3\t%4").arg(e.time, e.level, e.module, e.message);
    }
    file.write(lines.join('\n').toUtf8());
    file.close();
  }

  void setRightSidebarCollapsed(bool collapsed) {
    if (!rightSidebar_) return;
    if (collapsed) {
      rightSidebarWidth_ = std::max(280, rightSidebar_->width());
      rightSidebar_->hide();
      if (rightToggleBtn_) registerIcon(rightToggleBtn_, "chevron-left", 16);
    } else {
      rightSidebar_->show();
      if (rightToggleBtn_) registerIcon(rightToggleBtn_, "chevron-right", 16);
      QTimer::singleShot(0, this, [this] {
        bodySplitter_->setSizes({std::max(400, bodySplitter_->width() - rightSidebarWidth_), rightSidebarWidth_});
      });
    }
    saveLayoutSettings();
  }

  void setLogCollapsed(bool collapsed) {
    if (!logBody_ || !logPanel_ || !mainSplitter_) return;
    if (collapsed) {
      const int h = logPanel_->height();
      if (h > 48) logExpandedHeight_ = h;
      logBody_->hide();
      logPanel_->setMaximumHeight(34);
      logPanel_->setMinimumHeight(34);
      if (logCollapseBtn_) registerIcon(logCollapseBtn_, "chevron-up", 14);
    } else {
      logBody_->show();
      logPanel_->setMinimumHeight(120);
      logPanel_->setMaximumHeight(QWIDGETSIZE_MAX);
      if (logCollapseBtn_) registerIcon(logCollapseBtn_, "chevron-down", 14);
      const int target = std::max(160, logExpandedHeight_);
      QTimer::singleShot(0, this, [this, target] {
        const int total = mainSplitter_->height();
        mainSplitter_->setSizes({std::max(200, total - target), target});
      });
    }
    saveLayoutSettings();
  }

  void loadLayoutSettings() {
    QSettings settings("Q4J", "KLineViewer");
    if (settings.value("layout/rightCollapsed", false).toBool()) setRightSidebarCollapsed(true);
    if (settings.value("layout/logCollapsed", false).toBool()) setLogCollapsed(true);
  }

  void saveLayoutSettings() const {
    if (autoSaveLayout_ && !autoSaveLayout_->isChecked()) return;
    QSettings settings("Q4J", "KLineViewer");
    settings.setValue("layout/rightCollapsed", rightSidebar_ && !rightSidebar_->isVisible());
    settings.setValue("layout/logCollapsed", logBody_ && !logBody_->isVisible());
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
    if (replayActive_ && !loadedCandles_.isEmpty()) {
      chart_->setReplayMarker(loadedCandles_.first().ms, true);
    } else {
      chart_->setReplayMarker(0, false);
    }
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
    if (replayToggle_) replayToggle_->setText(replayActive_ ? "退出回放" : "K线回放");
    if (replayPlay_) registerIcon(replayPlay_, replayTimer_.isActive() ? "pause" : "play", 16);
    const QString startText = loadedCandles_.isEmpty() ? "--" : formatDisplayTime(loadedCandles_.first().ms, "yyyy-MM-dd HH:mm");
    const qint64 cursor = replayCursorMs();
    const QString curText = formatDisplayTime(cursor, "yyyy-MM-dd HH:mm");
    if (replayStartLabel_) replayStartLabel_->setText("起点 " + startText);
    if (replayCurrentLabel_) replayCurrentLabel_->setText("当前 " + curText);
    if (infoStart_) infoStart_->setText("数据起点：" + startText);
    updateReplaySlider();
  }

  void updateReplaySlider() {
    if (!replaySlider_ || loadedCandles_.isEmpty()) return;
    const qint64 first = loadedCandles_.first().ms;
    const qint64 last = loadedCandles_.last().ms;
    if (last <= first) return;
    const double t = double(replayCursorMs() - first) / double(last - first);
    QSignalBlocker blocker(replaySlider_);
    replaySlider_->setValue(std::clamp(static_cast<int>(t * 1000), 0, 1000));
  }

  int replayIntervalForSpeed(int speed) const {
    return std::max(20, 650 / std::max(1, speed));
  }

  void setReplaySpeed(int speed) {
    replaySpeedValue_ = speed;
    replayTimer_.setInterval(replayIntervalForSpeed(speed));
    if (replaySpeed_) replaySpeed_->setText(QString("%1x ▾").arg(speed));
    if (infoSpeed_) infoSpeed_->setText(QString("回放速度：%1x").arg(speed));
  }

  void replayJumpToStart() {
    if (loadedCandles_.isEmpty()) return;
    if (!replayActive_ && replayToggle_) replayToggle_->setChecked(true);
    replayTime_->setDateTime(replayDateTimeFromMs(loadedCandles_.first().ms));
  }

  void replayJumpToEnd() {
    if (loadedCandles_.isEmpty()) return;
    if (!replayActive_ && replayToggle_) replayToggle_->setChecked(true);
    replayTime_->setDateTime(replayDateTimeFromMs(loadedCandles_.last().ms));
  }

  void stepReplayBackward() {
    if (!replayTime_ || loadedCandles_.isEmpty()) return;
    if (!replayActive_ && replayToggle_) replayToggle_->setChecked(true);
    const qint64 cursor = replayCursorMs();
    auto it = std::lower_bound(loadedCandles_.begin(), loadedCandles_.end(), cursor, [](const Candle &candle, qint64 ms) {
      return candle.ms < ms;
    });
    if (it == loadedCandles_.begin()) return;
    --it;
    if (it->ms >= cursor && it != loadedCandles_.begin()) --it;
    replayStepping_ = true;
    replayTime_->setDateTime(replayDateTimeFromMs(it->ms));
    replayStepping_ = false;
    if (replayActive_) applyReplayView();
  }

  void seekReplay(int sliderValue) {
    if (loadedCandles_.isEmpty()) return;
    if (!replayActive_ && replayToggle_) replayToggle_->setChecked(true);
    const qint64 first = loadedCandles_.first().ms;
    const qint64 last = loadedCandles_.last().ms;
    const qint64 target = first + static_cast<qint64>(double(last - first) * sliderValue / 1000.0);
    replayTime_->setDateTime(replayDateTimeFromMs(target));
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
    events_->setText("事件 --");
    range_->setText("可视范围 --");
    lastRangeStartMs_ = 0;
    lastRangeEndMs_ = 0;
    lastRangeFirstIndex_ = -1;
    lastRangeLastIndex_ = -1;
    loadedCandles_.clear();
    loadedEventCount_ = 0;
    replayLoadCursorMs_ = 0;
    replayTimer_.stop();
    if (replayPlay_) replayPlay_->setChecked(false);
    chart_->clearMessage();
    chart_->setCandles({});
    chart_->setOverlayEvents(QJsonArray{});
    updateLegendSymbol();
    updateLoadStrategyButton();
    client_.load(symbol_->text(), currentTimeframe_, selectedStrategyName());
  }

  QString timeframeLabel(const QString &token) const {
    static const QHash<QString, QString> labels = {
      {"1S", "1s"}, {"5S", "5s"}, {"15S", "15s"}, {"30S", "30s"},
      {"1M", "1m"}, {"2M", "2m"}, {"3M", "3m"}, {"5M", "5m"}, {"10M", "10m"}, {"15M", "15m"}, {"30M", "30m"},
      {"1H", "1h"}, {"2H", "2h"}, {"4H", "4h"}, {"6H", "6h"}, {"12H", "12h"},
      {"1D", "1D"}, {"1W", "1W"}, {"1MO", "1M"}};
    return labels.value(token, token);
  }

  void selectTimeframe(const QString &token) {
    currentTimeframe_ = token;
    if (timeframeButtons_.contains(token)) {
      timeframeButtons_.value(token)->setChecked(true);
      moreTimeframe_->setText("更多 ▾");
    } else {
      if (auto *checked = timeframeGroup_->checkedButton()) {
        QSignalBlocker blocker(timeframeGroup_);
        checked->setChecked(false);
      }
      moreTimeframe_->setText(timeframeLabel(token) + " ▾");
    }
    updateLegendSymbol();
    QTimer::singleShot(0, this, &MainWindow::refresh);
  }

  void updateLegendSymbol() {
    if (!legendSymbol_) return;
    legendSymbol_->setText(QString("%1 · %2 · %3")
      .arg(symbol_->text(), timeframeLabel(currentTimeframe_), QString("Binance")));
    if (summarySymbol_) summarySymbol_->setText(symbol_->text());
  }

  ChartWidget *chart_ = nullptr;
  QFrame *titleBar_ = nullptr;
  QFrame *annotationToolbar_ = nullptr;
  QLineEdit *symbol_ = nullptr;
  QComboBox *strategy_ = nullptr;
  QComboBox *timeZone_ = nullptr;
  QDateTimeEdit *replayTime_ = nullptr;
  QPushButton *indicators_ = nullptr;
  QPushButton *customIndicators_ = nullptr;
  QPushButton *replayToggle_ = nullptr;
  QPushButton *replayPlay_ = nullptr;
  QPushButton *replayStep_ = nullptr;
  QPushButton *replayHome_ = nullptr;
  QPushButton *replayPrev_ = nullptr;
  QPushButton *replayEnd_ = nullptr;
  QPushButton *replaySpeed_ = nullptr;
  QSlider *replaySlider_ = nullptr;
  QLabel *replayStartLabel_ = nullptr;
  QLabel *replayCurrentLabel_ = nullptr;
  QLabel *replayTitleLabel_ = nullptr;
  QPushButton *backend_ = nullptr;
  QPushButton *updateButton_ = nullptr;
  QToolButton *magnetButton_ = nullptr;
  QPushButton *refresh_ = nullptr;
  QPushButton *notifyButton_ = nullptr;
  QPushButton *settingsGear_ = nullptr;
  QPushButton *minimize_ = nullptr;
  QPushButton *maximize_ = nullptr;
  QPushButton *close_ = nullptr;
  QLabel *ohlc_ = nullptr;
  QLabel *range_ = nullptr;
  QLabel *events_ = nullptr;
  QLabel *legendSymbol_ = nullptr;
  QLabel *summarySymbol_ = nullptr;
  QLabel *priceLabel_ = nullptr;
  QLabel *changeLabel_ = nullptr;
  QLabel *high24_ = nullptr;
  QLabel *low24_ = nullptr;
  QLabel *vol24_ = nullptr;
  // Timeframe toolbar
  QButtonGroup *timeframeGroup_ = nullptr;
  QHash<QString, QPushButton *> timeframeButtons_;
  QPushButton *moreTimeframe_ = nullptr;
  QString currentTimeframe_ = "15M";
  // Indicators / subcharts
  QMenu *indicatorMenu_ = nullptr;
  QAction *fvgMenuAction_ = nullptr;
  QHash<QString, QAction *> indicatorActions_;
  QHash<QString, QFrame *> subcharts_;
  QWidget *subchartContainer_ = nullptr;
  QVBoxLayout *subchartLayout_ = nullptr;
  // Layout containers
  QSplitter *mainSplitter_ = nullptr;
  QSplitter *bodySplitter_ = nullptr;
  QSplitter *chartSplitter_ = nullptr;
  QWidget *rightSidebar_ = nullptr;
  QTabWidget *rightTabs_ = nullptr;
  QPushButton *rightToggleBtn_ = nullptr;
  int rightSidebarWidth_ = 300;
  // Strategy sidebar
  QTableWidget *strategyTable_ = nullptr;
  QPushButton *loadStrategyBtn_ = nullptr;
  QPushButton *refreshStrategyBtn_ = nullptr;
  QLabel *infoStart_ = nullptr;
  QLabel *infoCurrent_ = nullptr;
  QLabel *infoSpeed_ = nullptr;
  QLabel *infoSource_ = nullptr;
  QLabel *infoLatency_ = nullptr;
  QLabel *serverInfoLabel_ = nullptr;
  int loadedEventCount_ = 0;
  // Settings popover
  QFrame *settingsPopover_ = nullptr;
  QCheckBox *themeToggle_ = nullptr;
  QCheckBox *autoSaveLayout_ = nullptr;
  // Log panel
  QFrame *logPanel_ = nullptr;
  QWidget *logBody_ = nullptr;
  QPushButton *logCollapseBtn_ = nullptr;
  QComboBox *logLevel_ = nullptr;
  QPushButton *logClear_ = nullptr;
  QPushButton *logExport_ = nullptr;
  QLabel *statInfo_ = nullptr;
  QLabel *statWarn_ = nullptr;
  QLabel *statError_ = nullptr;
  QLabel *statDebug_ = nullptr;
  QVector<LogEntry> logEntries_;
  QTableWidget *logTable_ = nullptr;
  int logInfo_ = 0;
  int logWarn_ = 0;
  int logError_ = 0;
  int logDebug_ = 0;
  int logExpandedHeight_ = 220;
  int replaySpeedValue_ = 10;
  QDialog *indicatorDialog_ = nullptr;
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
  QVector<QAbstractButton *> iconButtons_;
  QVector<QToolButton *> drawingButtons_;
  QStatusBar *statusBar_ = nullptr;
  QLabel *sbConnection_ = nullptr;
  QLabel *sbBackend_ = nullptr;
  QLabel *sbRealtime_ = nullptr;
  QLabel *sbLatency_ = nullptr;
  QFrame *replayBar_ = nullptr;
};
