#ifndef IMAGEVIEWER_H
#define IMAGEVIEWER_H

#include <QGraphicsView>
#include <QImage>

class QGraphicsPixmapItem;

class ImageViewer : public QGraphicsView
{
    Q_OBJECT
public:
    explicit ImageViewer(QWidget *parent = nullptr);
    void setImage(const QImage &image);
    bool hasImage() const;
    double zoomFactor() const;

public slots:
    void fitImage();
    void actualSize();

signals:
    void zoomChanged(double factor);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QGraphicsPixmapItem *m_pixmap = nullptr;
    bool m_fit = true;
};

#endif
