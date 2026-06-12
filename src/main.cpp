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

int main(int argc, char **argv) {
  // QApplication (not QGuiApplication) is retained so the chart's QMenu context
  // menus and the style-editing QDialog continue to work as top-level widgets.
  QApplication app(argc, argv);

  const QString bundledFontFamily = loadBundledFonts();
  const QString appFontFamily = bundledFontFamily.isEmpty() ? systemUiFontFamily() : bundledFontFamily;
  QFont appFont(appFontFamily);
  appFont.setPointSize(10);
  appFont.setWeight(QFont::Normal);
  appFont.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::PreferQuality));
  app.setFont(appFont);

  QQuickStyle::setStyle("Basic");

  qmlRegisterType<ChartItem>("AlgoHub", 1, 0, "ChartItem");

  ThemeProvider theme;
  AppController controller;
  controller.setDark(theme.dark());

  QQmlApplicationEngine engine;
  engine.addImageProvider(QStringLiteral("icon"), new IconImageProvider);
  engine.rootContext()->setContextProperty("controller", &controller);
  engine.rootContext()->setContextProperty("theme", &theme);

  engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
  if (engine.rootObjects().isEmpty()) return -1;

  return app.exec();
}
