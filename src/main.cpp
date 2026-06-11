#include <QtWidgets>
#include <QtNetwork>
#include <QtWebSockets/QWebSocket>
#include <QOpenGLWidget>
#if Q4J_HAS_QJS_ENGINE
#include <QJSEngine>
#include <QJSValueIterator>
#endif
#include <QDesktopServices>
#include <QFontDatabase>
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
#include "app/chart_widget.hpp"
#include "app/candle_client.hpp"
#include "app/main_window.hpp"

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  const QString bundledFontFamily = loadBundledFonts();
  const QString appFontFamily = bundledFontFamily.isEmpty() ? systemUiFontFamily() : bundledFontFamily;
  QFont appFont(appFontFamily);
  appFont.setPointSize(10);
  appFont.setWeight(QFont::Normal);
  appFont.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::PreferQuality));
  app.setFont(appFont);
  MainWindow window;
  window.show();
  return app.exec();
}
