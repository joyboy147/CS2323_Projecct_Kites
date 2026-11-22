/**
 * @file cause_bar.h
 * @brief A thin stacked bar showing how lost cycles split between causes.
 */
#pragma once

#include <QColor>
#include <QString>
#include <QWidget>
#include <vector>

namespace Kites
{
/** @brief Horizontal stacked bar; each segment's width is proportional to its value. */
class CauseBar : public QWidget
{
  public:
    struct Segment
    {
        double value{};
        QColor color;
        QString label;
    };

    explicit CauseBar(QWidget *parent = nullptr);
    void setSegments(const std::vector<Segment> &segments);
    QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    std::vector<Segment> m_segments;
};
} // namespace Kites
