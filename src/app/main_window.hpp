#pragma once

#include "chart_widget.hpp"
#include "candle_client.hpp"

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

  QPushButton *annotationButton(const QString &tip, ChartWidget::AnnotationTool tool) {
    auto *button = new QPushButton;
    button->setObjectName("annotationToolButton");
    button->setCheckable(true);
    button->setFixedSize(42, 42);
    button->setIcon(annotationIcon(tool));
    button->setIconSize(QSize(26, 26));
    button->setToolTip(tip);
    annotationGroup_->addButton(button, static_cast<int>(tool));
    return button;
  }

  QFrame *toolbarGroup(std::initializer_list<QWidget *> widgets) {
    auto *group = new QFrame;
    group->setObjectName("toolbarGroup");
    auto *layout = new QHBoxLayout(group);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);
    for (QWidget *widget : widgets) layout->addWidget(widget);
    return group;
  }

  QFrame *buildAnnotationToolbar() {
    auto *toolbar = new QFrame;
    toolbar->setObjectName("annotationToolbar");
    toolbar->setFixedWidth(56);
    auto *layout = new QVBoxLayout(toolbar);
    layout->setContentsMargins(7, 8, 7, 8);
    layout->setSpacing(8);
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
    magnetButton_->setFixedSize(42, 42);
    magnetButton_->setIcon(magnetIcon());
    magnetButton_->setIconSize(QSize(26, 26));
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
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);
    setCentralWidget(root);

    titleBar_ = new QFrame;
    titleBar_->setObjectName("titleBar");
    titleBar_->installEventFilter(this);
    auto *titleLayout = new QHBoxLayout(titleBar_);
    titleLayout->setContentsMargins(12, 0, 10, 0);
    titleLayout->setSpacing(10);
    auto *titleIcon = new QLabel;
    titleIcon->setObjectName("titleIcon");
    titleIcon->setPixmap(QIcon(":/app-icon.svg").pixmap(34, 34));
    titleIcon->setFixedSize(38, 38);
    auto *titleCopy = new QWidget;
    titleCopy->setObjectName("titleCopy");
    auto *titleCopyLayout = new QVBoxLayout(titleCopy);
    titleCopyLayout->setContentsMargins(0, 0, 0, 0);
    titleCopyLayout->setSpacing(0);
    auto *titleText = new QLabel("执行地图");
    titleText->setObjectName("titleText");
    auto *titleSubText = new QLabel("Execution Map");
    titleSubText->setObjectName("titleSubText");
    titleCopyLayout->addWidget(titleText);
    titleCopyLayout->addWidget(titleSubText);
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
    titleLayout->addWidget(titleIcon);
    titleLayout->addWidget(titleCopy);
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
    titleLayout->addWidget(titleIcon);
    titleLayout->addWidget(titleCopy);
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
    headerLayout->setContentsMargins(8, 8, 8, 8);
    headerLayout->setSpacing(10);

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
    replayToggle_ = new QPushButton("回放");
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
    refresh_ = new QPushButton("⟳ 刷新");
    refresh_->setObjectName("refreshButton");
    status_ = new QLabel("连接中");
    status_->setObjectName("status");
    symbol_->setFixedWidth(156);
    interval_->setFixedWidth(82);
    settings_->setFixedWidth(88);
    indicators_->setFixedWidth(64);
    replayToggle_->setFixedWidth(74);
    replayTime_->setFixedWidth(168);
    replayPlay_->setFixedWidth(44);
    replayStep_->setFixedWidth(44);
    backend_->setFixedWidth(104);
    wsLogButton_->setFixedWidth(78);
    updateButton_->setFixedWidth(88);
    theme_->setFixedWidth(44);
    refresh_->setFixedWidth(82);
    status_->setFixedWidth(118);
    headerLayout->addWidget(toolbarGroup({symbol_, interval_}));
    headerLayout->addWidget(toolbarGroup({settings_, indicators_}));
    headerLayout->addWidget(toolbarGroup({replayToggle_, replayTime_, replayPlay_, replayStep_}));
    headerLayout->addWidget(toolbarGroup({backend_, wsLogButton_, updateButton_}));
    headerLayout->addStretch(1);
    headerLayout->addWidget(toolbarGroup({theme_, refresh_, status_}));
    layout->addWidget(header);

    auto *chartRow = new QWidget;
    chartRow->setObjectName("chartRow");
    auto *chartRowLayout = new QHBoxLayout(chartRow);
    chartRowLayout->setContentsMargins(0, 0, 0, 0);
    chartRowLayout->setSpacing(10);
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
    footerLayout->setContentsMargins(14, 7, 10, 7);
    footerLayout->setSpacing(22);
    ohlc_ = new QLabel("OHLC:  --   --   --   --");
    range_ = new QLabel("可视范围： --");
    events_ = new QLabel("事件数： 0");
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
    settingsDialog_->setMinimumSize(420, 150);
    auto *layout = new QFormLayout(settingsDialog_);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(14);
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
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    layout->addRow("策略", strategy_);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, settingsDialog_, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
      saveStrategySettings();
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
    indicatorErrorText_->setObjectName("debugLog");
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
    connect(interval_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
      QTimer::singleShot(0, this, &MainWindow::refresh);
    });
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
      if (loadedCandles_.isEmpty()) {
        chart_->showLoadError("没有K线数据");
        updateIndicatorErrorText();
        return;
      }
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
    const QString css = (dark_ ? darkCss() : lightCss()) + modernControlsCss(dark_);
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
    client_.load(symbol_->text(), interval_->currentText(), selectedStrategyName());
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
