#include "ImageViewer.h"
#include "ImageNavigation.h"

#include <QGraphicsPixmapItem>
#include <QPainter>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>

ImageViewer::ImageViewer(QWidget *parent) : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    m_pixmap = scene()->addPixmap(QPixmap());
    setFrameShape(QFrame::NoFrame);
    setBackgroundBrush(QColor("#25282b"));
    setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setMinimumSize(160, 180);
    setAcceptDrops(false);
    viewport()->setAcceptDrops(false);
    m_navigation = new ImageNavigation(viewport(), [this] { return hasImage(); },
        [this](double factor, const QPointF &from, const QPointF &to) { navigate(factor, from, to); });
}

void ImageViewer::setImage(const QImage &image)
{
    {
        const QScopedValueRollback changing(m_changingView, true);
        m_navigation->reset();
        m_pixmap->setPixmap(QPixmap::fromImage(image));
        scene()->setSceneRect(m_pixmap->boundingRect());
        if (m_fit) fitImage();
    }
    if (!m_changingView) emit zoomChanged(zoomFactor());
    viewport()->update();
}

bool ImageViewer::hasImage() const { return !m_pixmap->pixmap().isNull(); }
double ImageViewer::zoomFactor() const { return transform().m11(); }

ImageViewer::ViewState ImageViewer::viewState() const
{
    return {m_fit, zoomFactor(), mapToScene(viewport()->rect().center())};
}

void ImageViewer::restoreViewState(const ViewState &state)
{
    m_navigation->reset();
    if (state.fitted) { fitImage(); return; }
    if (!hasImage() || !std::isfinite(state.zoom) || state.zoom <= 0.0) return;
    {
        const QScopedValueRollback changing(m_changingView, true);
        m_fit = false;
        setTransformationAnchor(QGraphicsView::NoAnchor);
        setTransform(QTransform::fromScale(state.zoom, state.zoom));
        centerOn(state.center);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    }
    notifyViewChanged();
}

void ImageViewer::syncNavigationFrom(const ImageViewer &source)
{
    if (&source == this || !hasImage() || !source.hasImage()) return;
    auto state = source.viewState();
    const QRectF from = source.m_pixmap->boundingRect();
    const QRectF to = m_pixmap->boundingRect();
    // Match image-relative positions and magnification, including resized previews.
    state.center = QPointF(state.center.x() / from.width() * to.width(),
                           state.center.y() / from.height() * to.height());
    state.zoom *= from.width() / to.width();
    restoreViewState(state);
}

void ImageViewer::fitImage()
{
    m_fit = true;
    if (!hasImage()) return;
    {
        const QScopedValueRollback changing(m_changingView, true);
        fitInView(m_pixmap, Qt::KeepAspectRatio);
    }
    notifyViewChanged();
}

void ImageViewer::actualSize()
{
    {
        const QScopedValueRollback changing(m_changingView, true);
        m_fit = false;
        resetTransform();
        centerOn(m_pixmap);
    }
    notifyViewChanged();
}

void ImageViewer::resizeEvent(QResizeEvent *event)
{
    {
        const QScopedValueRollback changing(m_changingView, true);
        QGraphicsView::resizeEvent(event);
        if (m_fit) fitImage();
    }
    if (!m_changingView) emit zoomChanged(zoomFactor());
}

void ImageViewer::navigate(double factor, const QPointF &from, const QPointF &to)
{
    if (!hasImage() || !std::isfinite(factor) || factor <= 0.0) return;
    const QPointF imageAnchor = mapToScene(from.toPoint());
    const double current = zoomFactor();
    const double next = std::clamp(current * factor, std::min(current, 0.02), std::max(current, 32.0));
    {
        const QScopedValueRollback changing(m_changingView, true);
        m_fit = false;
        setTransformationAnchor(QGraphicsView::NoAnchor);
        scale(next / current, next / current);
        const QPointF movedAnchor = mapToScene(to.toPoint());
        centerOn(mapToScene(viewport()->rect().center()) + imageAnchor - movedAnchor);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    }
    notifyViewChanged();
}

void ImageViewer::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    if (!m_changingView && hasImage()) {
        m_fit = false;
        emit navigationChanged();
    }
}

void ImageViewer::notifyViewChanged()
{
    if (m_changingView) return;
    emit zoomChanged(zoomFactor());
    emit navigationChanged();
}

void ImageViewer::paintEvent(QPaintEvent *event)
{
    QGraphicsView::paintEvent(event);
    if (!hasImage()) {
        QPainter painter(viewport());
        painter.setPen(QColor("#929b9f"));
        painter.drawText(viewport()->rect(), Qt::AlignCenter, "No image");
    }
}
