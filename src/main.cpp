#include <QtWidgets>
#include <QtNetwork>
#include <QtWebSockets/QWebSocket>
#if Q4J_HAS_QJS_ENGINE
#include <QJSEngine>
#include <QJSValueIterator>
#endif
#include <QDesktopServices>
#include <QFontDatabase>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
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

#include "app/core.hpp"
#include "app/chart_item.hpp"
#include "app/candle_client.hpp"
#include "app/qml_support.hpp"
#include "app/models.hpp"
#include "app/app_controller.hpp"
#include "app/workspace.hpp"

int main(int argc, char **argv) {
  // QApplication (not QGuiApplication) is retained so the chart's QMenu context
  // menus and the style-editing QDialog continue to work as top-level widgets.
  QApplication app(argc, argv);

  const QString bundledFontFamily = loadBundledFonts();
  const QString appFontFamily = bundledFontFamily.isEmpty() ? systemUiFontFamily() : bundledFontFamily;
  QFont appFont(appFontFamily);
  appFont.setPointSize(10);
  // Medium weight + native text rendering keeps glyphs crisp on Windows, where
  // the distance-field default looked thin/blurry.
  appFont.setWeight(QFont::Medium);
  appFont.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::PreferQuality));
  app.setFont(appFont);

  QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
  QQuickStyle::setStyle("Basic");

  ThemeProvider theme;
  Workspace controller;
  controller.setDark(theme.dark());

  QQmlApplicationEngine engine;
  // Cover both module resource layouts (qrc:/qt/qml/... and qrc:/...).
  engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
  engine.addImportPath(QStringLiteral("qrc:/"));
  engine.addImageProvider(QStringLiteral("icon"), new IconImageProvider);
  engine.rootContext()->setContextProperty("controller", &controller);
  engine.rootContext()->setContextProperty("theme", &theme);

  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                   [] { qWarning("AlgoHub: failed to create Main.qml root object"); },
                   Qt::QueuedConnection);

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  engine.loadFromModule("AlgoHub", "Main");
#else
  const QStringList qmlEntrypoints{
    QStringLiteral(":/qt/qml/AlgoHub/Main.qml"),
    QStringLiteral(":/AlgoHub/Main.qml"),
    QStringLiteral(":/AlgoHub/qml/Main.qml")
  };
  for (const QString &entrypoint : qmlEntrypoints) {
    if (!QFile::exists(entrypoint)) continue;
    engine.load(QUrl(QStringLiteral("qrc%1").arg(entrypoint)));
    break;
  }
#endif
  if (engine.rootObjects().isEmpty()) {
    qWarning("AlgoHub: QML engine produced no root objects");
    return -1;
  }

  return app.exec();
}
