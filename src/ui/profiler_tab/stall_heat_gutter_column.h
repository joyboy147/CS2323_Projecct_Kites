/**
 * @file stall_heat_gutter_column.h
 * @brief Profiler-editor gutter showing the cycles each line lost to stalls and flushes.
 */
#pragma once
#include "profiler_editor.h"
#include "ui/common/gutter_column.h"
#include "ui/theme/theme_manager.h"
#include <QPaintEvent>
#include <QPainter>

namespace Kites
{
/** @brief Lost cycles per line on a cache-miss-coloured background scaled by the maximum. */
class StallHeatGutterColumn : public GutterColumn
{
  public:
    explicit StallHeatGutterColumn(ProfilerEditor *editor)
        : GutterColumn(editor), m_profilerEditor(editor)
    {
    }

    QSize sizeHint() const override
    {
        if (m_profilerEditor->m_lineStallCycles.empty())
            return QSize(0, 0);
        const int digits = QString::number(m_profilerEditor->m_maxStallCycles).length();
        return QSize(fontMetrics().horizontalAdvance(QLatin1Char('9')) * (digits + 1) + 10, 0);
    }

  protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter(this);
        const auto &themeManager = ThemeManager::getInstance();
        painter.fillRect(event->rect(), themeManager.getEditorBackgroundColor());
        const auto &stalls = m_profilerEditor->m_lineStallCycles;
        if (stalls.empty())
            return;

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = blockTop(block);
        int bottom = top + blockHeight(block);
        while (block.isValid() && top <= event->rect().bottom())
        {
            const auto it = stalls.find(blockNumber + 1);
            if (block.isVisible() && bottom >= event->rect().top() && it != stalls.end() &&
                it->second > 0)
            {
                QColor heat = themeManager.getEditorErrorColor();
                const double share = static_cast<double>(it->second) /
                                     static_cast<double>(m_profilerEditor->m_maxStallCycles);
                heat.setAlpha(40 + static_cast<int>(160 * share));
                painter.fillRect(0, top, width(), bottom - top, heat);
                painter.setPen(themeManager.getEditorForegroundColor());
                painter.drawText(0, top, width() - 5, fontMetrics().height(), Qt::AlignRight,
                                 QString::number(it->second));
            }
            block = block.next();
            top = bottom;
            bottom = top + blockHeight(block);
            ++blockNumber;
        }
    }

  private:
    ProfilerEditor *m_profilerEditor;
};
} // namespace Kites
