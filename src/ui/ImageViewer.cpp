#include "ImageViewer.h"

#include <QGraphicsPixmapItem>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
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
}

void ImageViewer::setImage(const QImage &image)
{
    m_pixmap->setPixmap(QPixmap::fromImage(image));
    scene()->setSceneRect(m_pixmap->boundingRect());
    if (m_fit) fitImage();
    viewport()->update();
}

bool ImageViewer::hasImage() const { return !m_pixmap->pixmap().isNull(); }
double ImageViewer::zoomFactor() const { return transform().m11(); }

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

void ImageViewer::wheelEvent(QWheelEvent *event)
{
    if (!hasImage()) return;
    const double factor = std::pow(1.15, event->angleDelta().y() / 120.0);
    const double next = zoomFactor() * factor;
    if (next >= 0.02 && next <= 32.0) {
        m_fit = false;
        scale(factor, factor);
        emit zoomChanged(zoomFactor());
    }
    event->accept();
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
