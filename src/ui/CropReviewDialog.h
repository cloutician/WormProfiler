#ifndef CROPREVIEWDIALOG_H
#define CROPREVIEWDIALOG_H

#include <QDialog>
#include <QImage>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>
#include <opencv2/core.hpp>

class QLabel;
class QPushButton;
class QSpinBox;

struct CropPreviewItem
{
    QString imagePath;
    QSize imageSize;
    QRect cropRect;
    QRect originalCropRect;
    bool predictionFound = false;
    QString statusText;
};

class CropImageView : public QWidget
{
    Q_OBJECT

public:
    explicit CropImageView(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void setCropRect(const QRect &cropRect);
    QRect cropRect() const;
    QSize minimumSizeHint() const override;

signals:
    void cropRectChanged(const QRect &cropRect);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    enum DragMode {
        DragNone,
        DragMove,
        DragLeft,
        DragRight,
        DragTop,
        DragBottom,
        DragTopLeft,
        DragTopRight,
        DragBottomLeft,
        DragBottomRight
    };

    QRectF imageDrawRect() const;
    QRectF cropWidgetRect() const;
    QPoint imagePointFromWidget(const QPoint &widgetPoint) const;
    QRect clampCropRect(const QRect &cropRect) const;
    DragMode hitTest(const QPoint &widgetPoint) const;
    static bool changesLeft(DragMode mode);
    static bool changesRight(DragMode mode);
    static bool changesTop(DragMode mode);
    static bool changesBottom(DragMode mode);

    QImage m_image;
    QRect m_cropRect;
    QRect m_pressCropRect;
    QPoint m_pressImagePoint;
    DragMode m_dragMode = DragNone;
    int m_minCropSize = 8;
};

class CropReviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CropReviewDialog(const QList<CropPreviewItem> &items, QWidget *parent = nullptr);

private slots:
    void updateCurrentCropFromView(const QRect &cropRect);
    void updateCurrentCropFromSpinBoxes();
    void resetCurrentCrop();
    void goPrevious();
    void goNext();
    void exportCrops();

private:
    void setCurrentIndex(int index);
    void loadCurrentImage();
    void refreshControls();
    void refreshNavigation();
    QRect clampedRectForCurrentImage(const QRect &cropRect) const;
    QImage matToDisplayImage(const cv::Mat &mat) const;

    QList<CropPreviewItem> m_items;
    CropImageView *m_imageView = nullptr;
    QLabel *m_headerLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QSpinBox *m_xSpinBox = nullptr;
    QSpinBox *m_ySpinBox = nullptr;
    QSpinBox *m_widthSpinBox = nullptr;
    QSpinBox *m_heightSpinBox = nullptr;
    QPushButton *m_previousButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QPushButton *m_exportButton = nullptr;
    int m_currentIndex = -1;
    bool m_updatingControls = false;
};

#endif // CROPREVIEWDIALOG_H
