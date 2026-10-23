#include "ImageNavigation.h"

#include <QLineF>
#include <QNativeGestureEvent>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <utility>

ImageNavigation::ImageNavigation(QWidget *surface, std::function<bool()> hasImage, Navigate navigate)
    : QObject(surface), m_hasImage(std::move(hasImage)), m_navigate(std::move(navigate))
{
    surface->setAttribute(Qt::WA_AcceptTouchEvents);
    surface->installEventFilter(this);
}

void ImageNavigation::reset()
{
    m_touchIds.clear();
    m_touchDistance = 0.0;
}

bool ImageNavigation::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::TouchCancel || event->type() == QEvent::TouchEnd) {
        const bool active = !m_touchIds.isEmpty();
        reset();
        if (active) { event->accept(); return true; }
        return false;
    }
    if (!m_hasImage()) {
        reset();
        return false;
    }
    if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate) {
        auto *touch = static_cast<QTouchEvent *>(event);
        QList<QEventPoint> points;
        for (const auto &point : touch->points()) {
            if (point.state() != QEventPoint::Released) points.append(point);
        }
        std::sort(points.begin(), points.end(), [](const auto &a, const auto &b) { return a.id() < b.id(); });
        if (points.size() > 2) points = points.first(2);
        QList<int> ids;
        QPointF center;
        for (const auto &point : points) { ids.append(point.id()); center += point.position(); }
        if (!points.isEmpty()) center /= points.size();
        const double distance = points.size() == 2 ? QLineF(points[0].position(), points[1].position()).length() : 0.0;
        // Rebase when fingers are added or lifted, rather than jumping to a new centroid.
        if (!ids.isEmpty() && ids == m_touchIds && event->type() != QEvent::TouchBegin) {
            const double scale = distance > 1.0 && m_touchDistance > 1.0 ? distance / m_touchDistance : 1.0;
            m_navigate(scale, m_touchCenter, center);
        }
        m_touchIds = ids;
        m_touchCenter = center;
        m_touchDistance = distance;
        event->accept();
        return true;
    }
    if (event->type() == QEvent::NativeGesture) {
        auto *gesture = static_cast<QNativeGestureEvent *>(event);
        const QPointF anchor = gesture->position();
        switch (gesture->gestureType()) {
        case Qt::ZoomNativeGesture:
            m_navigate(1.0 + gesture->value(), anchor, anchor);
            break;
        case Qt::PanNativeGesture:
            m_navigate(1.0, anchor, anchor + gesture->delta());
            break;
        case Qt::BeginNativeGesture:
        case Qt::EndNativeGesture:
            reset();
            break;
        default:
            return false;
        }
        event->accept();
        return true;
    }
    if (event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        const QPointF anchor = wheel->position();
        if (!wheel->pixelDelta().isNull() && !(wheel->modifiers() & Qt::ControlModifier)) {
            m_navigate(1.0, anchor, anchor + wheel->pixelDelta());
        } else {
            const double steps = !wheel->pixelDelta().isNull() ? wheel->pixelDelta().y() / 120.0
                                                              : wheel->angleDelta().y() / 120.0;
            if (steps != 0.0) m_navigate(std::pow(1.15, steps), anchor, anchor);
        }
        event->accept();
        return true;
    }
    return false;
}
