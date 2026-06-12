#pragma once

#include <QColor>
#include <QString>

// Centralized design tokens for the AlgoHub trading terminal.
//
// The dark palette is derived from an OKLCH ramp (hue ~255, low chroma) so the
// base surface is a deep desaturated blue-slate rather than pure black, with an
// even perceptual step between surface levels and high-contrast text. Values
// are baked to sRGB hex for use in Qt stylesheets and QPainter.
namespace Theme {

// ---- Dark palette (primary) -------------------------------------------------
// surfaces: oklch(0.17..0.27, 0.02, 255)
inline QString bgApp() { return "#0F141B"; }       // oklch(0.18 0.02 255)
inline QString bgPanel() { return "#151C26"; }     // oklch(0.215 0.02 255)
inline QString bgPanel2() { return "#19212C"; }    // oklch(0.235 0.02 255)
inline QString bgToolbar() { return "#121821"; }   // oklch(0.20 0.02 255)
inline QString bgElevated() { return "#1E2833"; }  // oklch(0.265 0.022 255)
inline QString bgHover() { return "#26313D"; }

inline QString borderSubtle() { return "rgba(130, 150, 175, 0.18)"; }
inline QString borderStrong() { return "rgba(120, 170, 255, 0.42)"; }

inline QString textPrimary() { return "#E7EDF4"; }    // oklch(0.94 0.01 255)
inline QString textSecondary() { return "#AAB7C6"; }  // oklch(0.77 0.02 255)
inline QString textMuted() { return "#76828F"; }      // oklch(0.60 0.02 255)

inline QString brandBlue() { return "#3B82F6"; }
inline QString brandBlueHover() { return "#5C9BFF"; }
inline QString brandBlueSoft() { return "rgba(59, 130, 246, 0.20)"; }

inline QString green() { return "#16C784"; }
inline QString red() { return "#F0616D"; }
inline QString orange() { return "#F5A623"; }
inline QString purple() { return "#A66CFF"; }
inline QString cyan() { return "#00D5D8"; }

// ---- Light palette ----------------------------------------------------------
inline QString lBgApp() { return "#EDF0F4"; }
inline QString lBgPanel() { return "#FFFFFF"; }
inline QString lBgElevated() { return "#F4F6F9"; }
inline QString lBgHover() { return "#E6EBF1"; }
inline QString lBorderSubtle() { return "rgba(30, 50, 80, 0.14)"; }
inline QString lBorderStrong() { return "rgba(37, 99, 235, 0.45)"; }
inline QString lTextPrimary() { return "#16202C"; }
inline QString lTextSecondary() { return "#4B5A6B"; }
inline QString lTextMuted() { return "#8794A2"; }
inline QString lBrandBlue() { return "#2563EB"; }

// ---- QColor accessors for the painter (dark) --------------------------------
inline QColor cBgApp() { return QColor("#0F141B"); }
inline QColor cBgPanel() { return QColor("#151C26"); }
inline QColor cBgPanel2() { return QColor("#19212C"); }
inline QColor cTextPrimary() { return QColor("#E7EDF4"); }
inline QColor cTextSecondary() { return QColor("#AAB7C6"); }
inline QColor cTextMuted() { return QColor("#76828F"); }
inline QColor cBrandBlue() { return QColor("#3B82F6"); }
inline QColor cGreen() { return QColor("#16C784"); }
inline QColor cRed() { return QColor("#F0616D"); }
inline QColor cOrange() { return QColor("#F5A623"); }
inline QColor cPurple() { return QColor("#A66CFF"); }
inline QColor cCyan() { return QColor("#00D5D8"); }
inline QColor cGrid() { return QColor(130, 155, 190, 34); }

}  // namespace Theme
