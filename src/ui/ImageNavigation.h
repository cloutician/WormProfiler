#pragma once

#include <QObject>
#include <QPointF>
#include <QList>
#include <functional>

class QWidget;

class ImageNavigation final : public QObject
{
public:
    using Navigate = std::function<void(double, const QPointF &, const QPointF &)>;
    ImageNavigation(QWidget *surface, std::function<bool()> hasImage, Navigate navigate);
    void reset();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    std::function<bool()> m_hasImage;
    Navigate m_navigate;
    QList<int> m_touchIds;
    QPointF m_touchCenter;
    double m_touchDistance = 0.0;
};
