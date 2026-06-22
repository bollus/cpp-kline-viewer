#pragma once

#include <QColor>
#include <QString>

// Centralized design tokens for the Q4J trading terminal.
namespace Theme {

// ---- Dark palette (primary) -------------------------------------------------
inline QString bgApp() { return "#070B10"; }       // app shell / chart canvas
inline QString bgPanel() { return "#0E141B"; }     // side / log panels
inline QString bgPanel2() { return "#151D27"; }    // nested panel surface
inline QString bgToolbar() { return "#0A1017"; }   // top nav / toolbars
inline QString bgElevated() { return "#192330"; }  // popups / dialogs
inline QString bgHover() { return "#1E2A38"; }

inline QString borderSubtle() { return "#1CA4B9D3"; }
inline QString borderStrong() { return "#6B35D0B5"; }

inline QString textPrimary() { return "#EAF2F8"; }
inline QString textSecondary() { return "#9FB1C5"; }
inline QString textMuted() { return "#66788D"; }

inline QString brandBlue() { return "#35D0B5"; }
inline QString brandBlueHover() { return "#5DE7D1"; }
inline QString brandBlueSoft() { return "#2635D0B5"; }

inline QString green() { return "#2FE6A6"; }
inline QString red() { return "#FF5D73"; }
inline QString orange() { return "#F7B955"; }
inline QString purple() { return "#A78BFA"; }
inline QString cyan() { return "#38BDF8"; }

// ---- Light palette ----------------------------------------------------------
inline QString lBgApp() { return "#EEF3F7"; }
inline QString lBgPanel() { return "#FFFFFF"; }
inline QString lBgElevated() { return "#F7FAFC"; }
inline QString lBgHover() { return "#E8F1F5"; }
inline QString lBorderSubtle() { return "#1A18304D"; }
inline QString lBorderStrong() { return "#47009180"; }
inline QString lTextPrimary() { return "#182435"; }
inline QString lTextSecondary() { return "#536273"; }
inline QString lTextMuted() { return "#8A9AAA"; }
inline QString lBrandBlue() { return "#009982"; }
inline QString lBrandBlueSoft() { return "#1A009982"; }

// ---- QColor accessors for the painter (dark) --------------------------------
inline QColor cBgApp() { return QColor("#070B10"); }
inline QColor cBgPanel() { return QColor("#0E141B"); }
inline QColor cBgPanel2() { return QColor("#151D27"); }
inline QColor cTextPrimary() { return QColor("#EAF2F8"); }
inline QColor cTextSecondary() { return QColor("#9FB1C5"); }
inline QColor cTextMuted() { return QColor("#66788D"); }
inline QColor cBrandBlue() { return QColor("#35D0B5"); }
inline QColor cGreen() { return QColor("#2FE6A6"); }
inline QColor cRed() { return QColor("#FF5D73"); }
inline QColor cOrange() { return QColor("#F7B955"); }
inline QColor cPurple() { return QColor("#A78BFA"); }
inline QColor cCyan() { return QColor("#38BDF8"); }
inline QColor cGrid() { return QColor(119, 144, 168, 24); }

}  // namespace Theme
