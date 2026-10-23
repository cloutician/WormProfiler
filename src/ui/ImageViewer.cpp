#include "ImageViewer.h"
#include "ImageNavigation.h"

#include <QGraphicsPixmapItem>
#include <QPainter>
#include <QResizeEvent>
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
    m_navigation->reset();
    m_pixmap->setPixmap(QPixmap::fromImage(image));
    scene()->setSceneRect(m_pixmap->boundingRect());
    if (m_fit) fitImage();
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
    m_fit = false;
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setTransform(QTransform::fromScale(state.zoom, state.zoom));
    centerOn(state.center);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    emit zoomChanged(zoomFactor());
}

void ImageViewer::fitImage()
{
    m_fit = true;
    if (!hasImage()) return;
    fitInView(m_pixmap, Qt::KeepAspectRatio);
    emit zoomChanged(zoomFactor());
}

void ImageViewer::actualSize()
{
    m_fit = false;
    resetTransform();
    centerOn(m_pixmap);
    emit zoomChanged(1.0);
}

void ImageViewer::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_fit) fitImage();
}

void ImageViewer::navigate(double factor, const QPointF &from, const QPointF &to)
{
    if (!hasImage() || !std::isfinite(factor) || factor <= 0.0) return;
    const QPointF imageAnchor = mapToScene(from.toPoint());
    const double current = zoomFactor();
    const double next = std::clamp(current * factor, std::min(current, 0.02), std::max(current, 32.0));
    m_fit = false;
    setTransformationAnchor(QGraphicsView::NoAnchor);
    scale(next / current, next / current);
    const QPointF movedAnchor = mapToScene(to.toPoint());
    centerOn(mapToScene(viewport()->rect().center()) + imageAnchor - movedAnchor);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    if (next != current) emit zoomChanged(zoomFactor());
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
