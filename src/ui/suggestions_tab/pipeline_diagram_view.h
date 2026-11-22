/**
 * @file pipeline_diagram_view.h
 * @brief Table view that draws a PipelineDiagram (instructions x cycles).
 */
#pragma once

#include "scheduler/pipeline_trace.h"

#include <QColor>
#include <QTableWidget>

namespace Kites
{
/** @brief Theme colours for the diagram cells. */
struct DiagramPalette
{
    QColor stage;
    QColor stall;
    QColor bubble;
    QColor flushed;
    QColor text;
};

/** Instruction x cycle pipeline diagram (rows = dynamic instructions, columns = cycles). */
class PipelineDiagramView : public QTableWidget
{
  public:
    static constexpr int kMaxColumns = 150;

    explicit PipelineDiagramView(QWidget *parent = nullptr);

    /** @brief Show `diagram` (at most kMaxColumns cycles). */
    void setDiagram(const PipelineDiagram &diagram, const DiagramPalette &palette);
    [[nodiscard]] bool columnsTruncated() const
    {
        return m_columnsTruncated;
    }

  private:
    bool m_columnsTruncated = false;
};
} // namespace Kites
