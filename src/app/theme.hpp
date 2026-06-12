#pragma once

#include <QColor>
#include <QString>

// Centralized design tokens for the AlgoHub cold blue-black trading terminal.
// Both the QSS stylesheet (MainWindow) and the custom QPainter chart
// (ChartWidget) read from these so the palette stays consistent.
namespace Theme {

// ---- Dark palette (primary) -------------------------------------------------
inline QString bgApp() { return "#07121F"; }
inline QString bgPanel() { return "#0B1826"; }
inline QString bgPanel2() { return "#0E2032"; }
inline QString bgToolbar() { return "#081421"; }
inline QString bgElevated() { return "#102538"; }

inline QString borderSubtle() { return "rgba(120, 160, 200, 0.16)"; }
inline QString borderStrong() { return "rgba(120, 180, 255, 0.35)"; }

inline QString textPrimary() { return "#DDE8F5"; }
inline QString textSecondary() { return "#8EA4BC"; }
inline QString textMuted() { return "#5F7488"; }

inline QString brandBlue() { return "#1677FF"; }
inline QString brandBlueHover() { return "#2F8CFF"; }
inline QString brandBlueSoft() { return "rgba(22, 119, 255, 0.18)"; }

inline QString green() { return "#16C784"; }
inline QString red() { return "#EF5350"; }
inline QString orange() { return "#F5A623"; }
inline QString purple() { return "#A66CFF"; }
inline QString cyan() { return "#00D5D8"; }

// ---- QColor accessors for the painter ---------------------------------------
inline QColor cBgApp() { return QColor("#07121F"); }
inline QColor cBgPanel() { return QColor("#0B1826"); }
inline QColor cBgPanel2() { return QColor("#0E2032"); }
inline QColor cTextPrimary() { return QColor("#DDE8F5"); }
inline QColor cTextSecondary() { return QColor("#8EA4BC"); }
inline QColor cTextMuted() { return QColor("#5F7488"); }
inline QColor cBrandBlue() { return QColor("#1677FF"); }
inline QColor cGreen() { return QColor("#16C784"); }
inline QColor cRed() { return QColor("#EF5350"); }
inline QColor cOrange() { return QColor("#F5A623"); }
inline QColor cPurple() { return QColor("#A66CFF"); }
inline QColor cCyan() { return QColor("#00D5D8"); }
inline QColor cGrid() { return QColor(120, 160, 200, 38); }

}  // namespace Theme
