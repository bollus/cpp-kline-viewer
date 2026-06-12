#pragma once

#include <QObject>
#include <QColor>
#include <QPixmap>
#include <QPainter>
#include <QUrlQuery>
#include <QQuickImageProvider>
#include <QtSvg/QSvgRenderer>

#include "theme.hpp"

// Renders bundled SVG icons (resources/icons/*.svg) tinted to an arbitrary
// colour. Mirrors MainWindow::tintedPixmap from the old Widgets UI so the QML
// shell can recolour glyphs per theme/state.
//
// QML usage:
//   Image { source: "image://icon/refresh?color=%23E7EDF4&px=18" }
class IconImageProvider : public QQuickImageProvider {
public:
  IconImageProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

  QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override {
    QString name = id;
    QColor color = Theme::cTextSecondary();
    int px = 22;

    const int q = id.indexOf('?');
    if (q >= 0) {
      name = id.left(q);
      const QStringList params = id.mid(q + 1).split('&', Qt::SkipEmptyParts);
      for (const QString &param : params) {
        const int eq = param.indexOf('=');
        if (eq < 0) continue;
        const QString key = param.left(eq);
        QString value = QUrl::fromPercentEncoding(param.mid(eq + 1).toUtf8());
        if (key == "color") {
          if (!value.startsWith('#') && value.size() == 6) value.prepend('#');
          const QColor parsed(value);
          if (parsed.isValid()) color = parsed;
        } else if (key == "px") {
          const int requested = value.toInt();
          if (requested > 0) px = requested;
        }
      }
    }
    if (requestedSize.isValid() && requestedSize.width() > 0) px = requestedSize.width();

    QSvgRenderer renderer(QStringLiteral(":/icons/%1.svg").arg(name));
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (renderer.isValid()) renderer.render(&p, QRectF(0, 0, px, px));
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(QRect(0, 0, px, px), color);
    p.end();

    if (size) *size = QSize(px, px);
    return pm;
  }
};

// Exposes the theme.hpp design tokens to QML and tracks the active dark/light
// mode. All colour properties resolve against the current mode, so QML bindings
// referring to Theme.bgApp etc. automatically refresh when `dark` toggles.
class ThemeProvider : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY changed)
  Q_PROPERTY(QString bgApp READ bgApp NOTIFY changed)
  Q_PROPERTY(QString bgPanel READ bgPanel NOTIFY changed)
  Q_PROPERTY(QString bgPanel2 READ bgPanel2 NOTIFY changed)
  Q_PROPERTY(QString bgToolbar READ bgToolbar NOTIFY changed)
  Q_PROPERTY(QString bgElevated READ bgElevated NOTIFY changed)
  Q_PROPERTY(QString bgHover READ bgHover NOTIFY changed)
  Q_PROPERTY(QString borderSubtle READ borderSubtle NOTIFY changed)
  Q_PROPERTY(QString borderStrong READ borderStrong NOTIFY changed)
  Q_PROPERTY(QString textPrimary READ textPrimary NOTIFY changed)
  Q_PROPERTY(QString textSecondary READ textSecondary NOTIFY changed)
  Q_PROPERTY(QString textMuted READ textMuted NOTIFY changed)
  Q_PROPERTY(QString brandBlue READ brandBlue NOTIFY changed)
  Q_PROPERTY(QString brandBlueHover READ brandBlueHover NOTIFY changed)
  Q_PROPERTY(QString brandBlueSoft READ brandBlueSoft NOTIFY changed)
  Q_PROPERTY(QString green READ green NOTIFY changed)
  Q_PROPERTY(QString red READ red NOTIFY changed)
  Q_PROPERTY(QString orange READ orange NOTIFY changed)
  Q_PROPERTY(QString purple READ purple NOTIFY changed)
  Q_PROPERTY(QString cyan READ cyan NOTIFY changed)

public:
  explicit ThemeProvider(QObject *parent = nullptr) : QObject(parent) {}

  bool dark() const { return dark_; }
  void setDark(bool value) {
    if (dark_ == value) return;
    dark_ = value;
    emit changed();
  }

  QString bgApp() const { return dark_ ? Theme::bgApp() : Theme::lBgApp(); }
  QString bgPanel() const { return dark_ ? Theme::bgPanel() : Theme::lBgPanel(); }
  QString bgPanel2() const { return dark_ ? Theme::bgPanel2() : Theme::lBgElevated(); }
  QString bgToolbar() const { return dark_ ? Theme::bgToolbar() : Theme::lBgPanel(); }
  QString bgElevated() const { return dark_ ? Theme::bgElevated() : Theme::lBgElevated(); }
  QString bgHover() const { return dark_ ? Theme::bgHover() : Theme::lBgHover(); }
  QString borderSubtle() const { return dark_ ? Theme::borderSubtle() : Theme::lBorderSubtle(); }
  QString borderStrong() const { return dark_ ? Theme::borderStrong() : Theme::lBorderStrong(); }
  QString textPrimary() const { return dark_ ? Theme::textPrimary() : Theme::lTextPrimary(); }
  QString textSecondary() const { return dark_ ? Theme::textSecondary() : Theme::lTextSecondary(); }
  QString textMuted() const { return dark_ ? Theme::textMuted() : Theme::lTextMuted(); }
  QString brandBlue() const { return dark_ ? Theme::brandBlue() : Theme::lBrandBlue(); }
  QString brandBlueHover() const { return Theme::brandBlueHover(); }
  QString brandBlueSoft() const { return Theme::brandBlueSoft(); }
  QString green() const { return Theme::green(); }
  QString red() const { return Theme::red(); }
  QString orange() const { return Theme::orange(); }
  QString purple() const { return Theme::purple(); }
  QString cyan() const { return Theme::cyan(); }

  // Convenience for QML: build an icon URL resolving to the current theme tint.
  Q_INVOKABLE QString icon(const QString &name, const QString &role = QStringLiteral("secondary"), int px = 18) const {
    QString hex;
    if (role == "primary") hex = textPrimary();
    else if (role == "brand") hex = brandBlue();
    else if (role == "muted") hex = textMuted();
    else if (role == "green") hex = green();
    else if (role == "red") hex = red();
    else hex = textSecondary();
    const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(hex));
    return QStringLiteral("image://icon/%1?color=%2&px=%3").arg(name, encoded, QString::number(px));
  }

signals:
  void changed();

private:
  bool dark_ = true;
};
