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
// Anchored to three base surfaces (IDE-style neutral greys):
//   chart background  #1E1E1F
//   panel surfaces    #2B2D30
//   navigation bar    #252526
// the rest are derived as steps around those bases.
inline QString bgApp() { return "#1E1E1F"; }       // chart / main canvas
inline QString bgPanel() { return "#2B2D30"; }     // side / log panels
inline QString bgPanel2() { return "#323438"; }    // nested panel surface
inline QString bgToolbar() { return "#252526"; }   // top nav / toolbars
inline QString bgElevated() { return "#2F3134"; }  // popups / dialogs
inline QString bgHover() { return "#3A3D41"; }

inline QString borderSubtle() { return "rgba(255, 255, 255, 0.08)"; }
inline QString borderStrong() { return "rgba(77, 142, 239, 0.34)"; }

inline QString textPrimary() { return "#DFE1E5"; }
inline QString textSecondary() { return "#A9ABB0"; }
inline QString textMuted() { return "#70737A"; }

// Accent softened + slightly desaturated to sit comfortably on the neutral
// grey surfaces (active states use the *Soft tint, not the solid accent).
inline QString brandBlue() { return "#4D8EEF"; }
inline QString brandBlueHover() { return "#6FA6F5"; }
inline QString brandBlueSoft() { return "rgba(77, 142, 239, 0.14)"; }

inline QString green() { return "#16C784"; }
inline QString red() { return "#F0616D"; }
inline QString orange() { return "#F5A623"; }
inline QString purple() { return "#A66CFF"; }
inline QString cyan() { return "#00D5D8"; }

// ---- Light palette ----------------------------------------------------------
inline QString lBgApp() { return "#EDF0F4"; }
inline QString lBgPanel() { return "#FFFFFF"; }
inline QString lBgElevated() { return "#F4F6F9"; }
inline QString lBgHover() { return "#EAEEF3"; }
inline QString lBorderSubtle() { return "rgba(30, 45, 75, 0.09)"; }
inline QString lBorderStrong() { return "rgba(59, 114, 214, 0.26)"; }
inline QString lTextPrimary() { return "#1F2733"; }
inline QString lTextSecondary() { return "#566071"; }
inline QString lTextMuted() { return "#8A95A3"; }
inline QString lBrandBlue() { return "#3B72D6"; }
inline QString lBrandBlueSoft() { return "rgba(59, 114, 214, 0.10)"; }

// ---- QColor accessors for the painter (dark) --------------------------------
inline QColor cBgApp() { return QColor("#1E1E1F"); }
inline QColor cBgPanel() { return QColor("#2B2D30"); }
inline QColor cBgPanel2() { return QColor("#323438"); }
inline QColor cTextPrimary() { return QColor("#DFE1E5"); }
inline QColor cTextSecondary() { return QColor("#A9ABB0"); }
inline QColor cTextMuted() { return QColor("#70737A"); }
inline QColor cBrandBlue() { return QColor("#4D8EEF"); }
inline QColor cGreen() { return QColor("#16C784"); }
inline QColor cRed() { return QColor("#F0616D"); }
inline QColor cOrange() { return QColor("#F5A623"); }
inline QColor cPurple() { return QColor("#A66CFF"); }
inline QColor cCyan() { return QColor("#00D5D8"); }
inline QColor cGrid() { return QColor(255, 255, 255, 20); }

}  // namespace Theme
