#pragma once

#include <QColor>
#include <QString>

// Centralized design tokens for the AlgoHub trading terminal.
namespace Theme {

// ---- Dark palette (primary) -------------------------------------------------
inline QString bgApp() { return "#1E1E1F"; }
inline QString bgPanel() { return "#2B2D30"; }
inline QString bgPanel2() { return "#323438"; }
inline QString bgToolbar() { return "#252526"; }
inline QString bgElevated() { return "#2F3134"; }
inline QString bgHover() { return "#3A3D41"; }

inline QString borderSubtle() { return "#14FFFFFF"; }
inline QString borderStrong() { return "#525C9BE0"; }

inline QString textPrimary() { return "#DFE1E5"; }
inline QString textSecondary() { return "#A9ABB0"; }
inline QString textMuted() { return "#70737A"; }

inline QString brandBlue() { return "#5C9BE0"; }
inline QString brandBlueHover() { return "#79B0EC"; }
inline QString brandBlueSoft() { return "#245C9BE0"; }

inline QString green() { return "#16C784"; }
inline QString red() { return "#F0616D"; }
inline QString orange() { return "#F7B955"; }
inline QString purple() { return "#A66CFF"; }
inline QString cyan() { return "#00D5D8"; }

// ---- Light palette ----------------------------------------------------------
inline QString lBgApp() { return "#EDF0F4"; }
inline QString lBgPanel() { return "#FFFFFF"; }
inline QString lBgElevated() { return "#F4F6F9"; }
inline QString lBgHover() { return "#EAEEF3"; }
inline QString lBorderSubtle() { return "#171E2D4B"; }
inline QString lBorderStrong() { return "#3D3F7AD0"; }
inline QString lTextPrimary() { return "#1F2733"; }
inline QString lTextSecondary() { return "#566071"; }
inline QString lTextMuted() { return "#8A95A3"; }
inline QString lBrandBlue() { return "#3F7AD0"; }
inline QString lBrandBlueSoft() { return "#1A3F7AD0"; }

// ---- QColor accessors for the painter (dark) --------------------------------
inline QColor cBgApp() { return QColor("#1E1E1F"); }
inline QColor cBgPanel() { return QColor("#2B2D30"); }
inline QColor cBgPanel2() { return QColor("#323438"); }
inline QColor cTextPrimary() { return QColor("#DFE1E5"); }
inline QColor cTextSecondary() { return QColor("#A9ABB0"); }
inline QColor cTextMuted() { return QColor("#70737A"); }
inline QColor cBrandBlue() { return QColor("#5C9BE0"); }
inline QColor cGreen() { return QColor("#16C784"); }
inline QColor cRed() { return QColor("#F0616D"); }
inline QColor cOrange() { return QColor("#F7B955"); }
inline QColor cPurple() { return QColor("#A66CFF"); }
inline QColor cCyan() { return QColor("#00D5D8"); }
inline QColor cGrid() { return QColor(255, 255, 255, 20); }

}  // namespace Theme
