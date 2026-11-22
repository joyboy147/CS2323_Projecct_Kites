#include "cause_bar.h"

#include <QPaintEvent>
#include <QPainter>
#include <QStringList>

namespace Kites
{
namespace
{
constexpr int kBarHeight = 12;
} // namespace

CauseBar::CauseBar(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(kBarHeight);
    setMaximumHeight(kBarHeight);
}

void CauseBar::setSegments(const std::vector<Segment> &segments)
{
    m_segments = segments;
    QStringList parts;
    for (const Segment &segment : m_segments)
        if (segment.value > 0)
            parts << segment.label;
    setToolTip(parts.join("\n"));
    update();
}

QSize CauseBar::sizeHint() const
{
    return QSize(200, kBarHeight);
}

void CauseBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    double total = 0;
    for (const Segment &segment : m_segments)
        total += segment.value;
    if (total <= 0)
        return;
    double x = 0;
    for (const Segment &segment : m_segments)
    {
        const double width = segment.value / total * this->width();
        painter.fillRect(QRectF(x, 0, width, height()), segment.color);
        x += width;
    }
}
} // namespace Kites
