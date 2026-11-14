#pragma once

#include <QtGlobal>

namespace lighttrack::timeline_metrics
{
inline constexpr qreal GAP = 8.0;
inline constexpr qreal RULER_HEIGHT = 30.0;
inline constexpr qreal LABEL_WIDTH = 238.0;
inline constexpr qreal TIMELINE_MIN_WIDTH = 900.0;
inline constexpr qreal EFFECTS_PANEL_WIDTH = 178.0;
inline constexpr qreal GRID_WIDTH = 80.0;
// Pixels per second: from a whole song on screen to millisecond detail.
inline constexpr qreal TIMELINE_ZOOM_MIN = 5.0;
inline constexpr qreal TIMELINE_ZOOM_MAX = 2000.0;
}
