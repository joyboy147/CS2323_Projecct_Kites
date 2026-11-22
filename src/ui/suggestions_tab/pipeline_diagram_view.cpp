#include "pipeline_diagram_view.h"

#include "ui/theme/font_manager.h"

#include <QBrush>
#include <QHeaderView>
#include <QStringList>
#include <QTableWidgetItem>
#include <algorithm>

namespace Kites
{
namespace
{
constexpr int kColumnWidth = 34;
constexpr int kRowHeight = 20;
} // namespace

PipelineDiagramView::PipelineDiagramView(QWidget *parent) : QTableWidget(parent)
{
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::NoSelection);
    setFocusPolicy(Qt::NoFocus);
    horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    horizontalHeader()->setDefaultSectionSize(kColumnWidth);
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    verticalHeader()->setDefaultSectionSize(kRowHeight);
    setFont(FontManager::getInstance().currentFont());
}

void PipelineDiagramView::setDiagram(const PipelineDiagram &diagram, const DiagramPalette &palette)
{
    clear();
    const int columns = std::min(static_cast<int>(diagram.cycleCount), kMaxColumns);
    m_columnsTruncated = static_cast<int>(diagram.cycleCount) > kMaxColumns;
    setRowCount(static_cast<int>(diagram.rows.size()));
    setColumnCount(columns);

    QStringList cycleLabels;
    for (int c = 0; c < columns; ++c)
        cycleLabels << QString::number(c + 1);
    setHorizontalHeaderLabels(cycleLabels);

    QStringList rowLabels;
    for (const DiagramRow &row : diagram.rows)
        rowLabels << QString::fromStdString(row.label);
    setVerticalHeaderLabels(rowLabels);

    for (int r = 0; r < static_cast<int>(diagram.rows.size()); ++r)
    {
        const DiagramRow &row = diagram.rows[static_cast<std::size_t>(r)];
        for (int c = 0; c < columns; ++c)
        {
            const DiagramCell &cell = row.cells[static_cast<std::size_t>(c)];
            if (cell.kind == CellKind::Empty)
                continue;

            auto *item = new QTableWidgetItem();
            item->setTextAlignment(Qt::AlignCenter);
            item->setForeground(QBrush(palette.text));
            switch (cell.kind)
            {
            case CellKind::Stage:
                item->setText(QString::fromLatin1(pipeStageName(cell.stage)));
                item->setBackground(QBrush(palette.stage));
                break;
            case CellKind::Stall:
                item->setText(QString::fromLatin1(pipeStageName(cell.stage)));
                item->setBackground(QBrush(palette.stall));
                item->setToolTip(row.stallNote.empty()
                                     ? QString("Stalled in %1")
                                           .arg(QString::fromLatin1(pipeStageName(cell.stage)))
                                     : QString::fromStdString(row.stallNote));
                break;
            case CellKind::Bubble:
                item->setText(QString::fromUtf8("—"));
                item->setBackground(QBrush(palette.bubble));
                item->setToolTip("Bubble inserted by the hazard detection unit");
                break;
            case CellKind::Flushed:
                item->setText(QString::fromUtf8("×"));
                item->setBackground(QBrush(palette.flushed));
                item->setToolTip("Flushed by a taken branch or jump");
                break;
            case CellKind::Empty:
                break;
            }
            setItem(r, c, item);
        }
    }
}
} // namespace Kites
