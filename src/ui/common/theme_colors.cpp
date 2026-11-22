#include "theme_colors.h"

#include "ui/theme/theme_manager.h"

namespace Kites
{
TabColors currentColors()
{
    ThemeManager &themes = ThemeManager::getInstance();
    TabColors colors{
        QColor("#9ece6a"),     QColor("#f7768e"), QColor("#565f89"), QColor("#f7768e"),
        themes.getTextColor(), QColor("#1f2335"), QColor(),          themes.getCurrentLineColor(),
        QColor("#565f89")};
    for (const ThemeData &theme : themes.availableThemes())
    {
        if (theme.id != themes.currentThemeId())
            continue;
        colors.moved = theme.cacheHitColor;
        colors.stall = theme.cacheMissColor;
        colors.bubble = theme.mutedForegroundColor;
        colors.flushed = theme.cacheMissColor;
        colors.surface = theme.surfaceColor;
        colors.muted = theme.mutedForegroundColor;
        break;
    }
    colors.moved.setAlpha(70);
    colors.stall.setAlpha(110);
    colors.bubble.setAlpha(60);
    colors.flushed.setAlpha(45);
    colors.blockTint = colors.text;
    colors.blockTint.setAlpha(14);
    return colors;
}
} // namespace Kites
