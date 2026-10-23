#ifndef IMAGEVIEWER_H
#define IMAGEVIEWER_H

#include <QGraphicsView>
#include <QImage>

class QGraphicsPixmapItem;
class ImageNavigation;

class ImageViewer : public QGraphicsView
{
    Q_OBJECT
public:
    struct ViewState {
        bool fitted = true;
        double zoom = 1.0;
        QPointF center;
    };

    explicit ImageViewer(QWidget *parent = nullptr);
    void setImage(const QImage &image);
    bool hasImage() const;
    double zoomFactor() const;
    ViewState viewState() const;
    void restoreViewState(const ViewState &state);
    void syncNavigationFrom(const ImageViewer &source);

public slots:
    void fitImage();
    void actualSize();

signals:
    void zoomChanged(double factor);
    void navigationChanged();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    QGraphicsPixmapItem *m_pixmap = nullptr;
    ImageNavigation *m_navigation = nullptr;
    void navigate(double factor, const QPointF &from, const QPointF &to);
    void notifyViewChanged();
    bool m_fit = true;
    bool m_changingView = false;
};

#endif
