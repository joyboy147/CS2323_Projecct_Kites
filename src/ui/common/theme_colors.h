/**
 * @file theme_colors.h
 * @brief Colours derived from the active Kites theme for the Suggestions and Profiler views.
 */
#pragma once

#include <QColor>

namespace Kites
{
/** @brief Translucent highlight colours for tables, diagrams and charts. */
struct TabColors
{
    QColor moved;       ///< Rows moved by a suggestion (cache-hit colour).
    QColor stall;       ///< Stall cells (cache-miss colour).
    QColor bubble;      ///< Pipeline bubbles (muted foreground).
    QColor flushed;     ///< Flushed instructions.
    QColor text;        ///< Normal text.
    QColor surface;     ///< Panel background (banners).
    QColor blockTint;   ///< Alternate-block tint.
    QColor currentLine; ///< Current-line colour (flush segments in the profiler).
    QColor muted;       ///< Opaque muted foreground (spurious stalls).
};

/** @brief Colours for the current theme, with fallbacks when the theme is unknown. */
TabColors currentColors();
} // namespace Kites
