#pragma once

#include <QColor>
#include <QJsonObject>
#include <QPolygonF>
#include <QString>
#include <QVector>
#include <limits>

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
