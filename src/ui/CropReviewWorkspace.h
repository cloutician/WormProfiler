#ifndef CROPREVIEWWORKSPACE_H
#define CROPREVIEWWORKSPACE_H

#include <QImage>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>
#include "CropReviewSession.h"
#include <opencv2/core.hpp>
#include <atomic>
#include <memory>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QCloseEvent;
template<typename T> class QFutureWatcher;

class CropImageView : public QWidget
{
    Q_OBJECT

public:
    explicit CropImageView(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void setCropRect(const QRect &cropRect);
    QRect cropRect() const;
    QImage cropPreview(const QSize &size) const;
    double zoomFactor() const;
    QSize minimumSizeHint() const override;

signals:
    void cropRectChanged(const QRect &cropRect);
    void zoomChanged(double factor);

public slots:
    void fitImage();
    void actualSize();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

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
    int m_minCropSize = 1;
    double m_zoom = 1.0;
    QPointF m_panOffset;
    QPointF m_panStart;
    bool m_panning = false;
};

class CropReviewWorkspace : public QWidget
{
    Q_OBJECT

public:
    explicit CropReviewWorkspace(const CropReviewSession &session = {}, QWidget *parent = nullptr,
                              const QString &sessionPath = QString());
    ~CropReviewWorkspace() override;
    void setSession(const CropReviewSession &session, const QString &path = QString());
    void setExternalBusy(bool busy);
    bool maybeSave();
    bool isBusy() const;
    bool hasSession() const;
    bool isDirty() const;
    QString sessionPath() const;
    QAction *saveAction() const;
    QAction *saveAsAction() const;
    QAction *exportAction() const;
    QAction *newAction() const;
    QAction *openAction() const;

public slots:
    void saveSession();
    void saveSessionAs();
    void exportCrops();
    void cancelExport();

signals:
    void newReviewRequested();
    void openSessionRequested();
    void openSourceRequested(const QString &path);
    void stateChanged();
    void busyChanged(bool busy);
    void jobProgress(int value, int maximum);
    void statusMessage(const QString &message);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void updateCurrentCropFromView(const QRect &cropRect);
    void updateCurrentCropFromSpinBoxes();
    void resetCurrentCrop();
    void goPrevious();
    void goNext();
    void applyFilter();

private:
    void setCurrentIndex(int index);
    void loadCurrentImage();
    void refreshControls();
    void refreshNavigation();
    QRect clampedRectForCurrentImage(const QRect &cropRect) const;
    QImage matToDisplayImage(const cv::Mat &mat) const;
    void setReviewState(CropReviewState state);
    void markCropChanged();
    void refreshListItem(int index);
    void loadThumbnail(int index, int generation);
    bool matchesFilter(int index) const;
    int adjacentVisibleIndex(int direction) const;
    bool writeSession();
    CropReviewSession currentSession() const;

    QList<CropPreviewItem> m_items;
    CropImageView *m_imageView = nullptr;
    QLabel *m_headerLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_predictionLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_cropPreview = nullptr;
    QLabel *m_modelLabel = nullptr;
    QListWidget *m_imageList = nullptr;
    QComboBox *m_filter = nullptr;
    QCheckBox *m_autoAdvance = nullptr;
    QSpinBox *m_xSpinBox = nullptr;
    QSpinBox *m_ySpinBox = nullptr;
    QSpinBox *m_widthSpinBox = nullptr;
    QSpinBox *m_heightSpinBox = nullptr;
    QPushButton *m_previousButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QAction *m_exportAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_saveAsAction = nullptr;
    QAction *m_newAction = nullptr;
    QAction *m_openAction = nullptr;
    QPushButton *m_approveButton = nullptr;
    QPushButton *m_rejectButton = nullptr;
    QPushButton *m_flagButton = nullptr;
    QPushButton *m_pendingButton = nullptr;
    QPushButton *m_resetButton = nullptr;
    QWidget *m_content = nullptr;
    QFutureWatcher<CropExportResult> *m_exportWatcher = nullptr;
    std::shared_ptr<std::atomic_bool> m_exportCancel;
    QString m_modelPath;
    QString m_sessionPath;
    int m_cropMarginPixels = 0;
    double m_predictionThreshold = 0.65;
    int m_currentIndex = -1;
    bool m_updatingControls = false;
    bool m_dirty = false;
    bool m_imageAvailable = false;
    bool m_exporting = false;
    bool m_externalBusy = false;
    int m_thumbnailGeneration = 0;
};

#endif // CROPREVIEWWORKSPACE_H
