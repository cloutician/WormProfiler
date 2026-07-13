#include "CropReviewDialog.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressDialog>
#include <QPushButton>
#include <QShortcut>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

CropImageView::CropImageView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(520, 420);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void CropImageView::setImage(const QImage &image)
{
    m_image = image;

    if (!m_image.isNull()) {
        m_cropRect = clampCropRect(m_cropRect);
    }

    update();
}

void CropImageView::setCropRect(const QRect &cropRect)
{
    m_cropRect = clampCropRect(cropRect);
    update();
}

QRect CropImageView::cropRect() const
{
    return m_cropRect;
}

QSize CropImageView::minimumSizeHint() const
{
    return QSize(640, 480);
}

void CropImageView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor(33, 35, 38));
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (m_image.isNull()) {
        painter.setPen(QColor(220, 220, 220));
        painter.drawText(rect(), Qt::AlignCenter, "No image selected");
        return;
    }

    const QRectF imageRect = imageDrawRect();
    painter.drawImage(imageRect, m_image);

    const QRectF cropRect = cropWidgetRect();

    if (!cropRect.isValid()) {
        return;
    }

    const QColor shade(0, 0, 0, 115);
    painter.fillRect(QRectF(imageRect.left(), imageRect.top(), imageRect.width(), cropRect.top() - imageRect.top()), shade);
    painter.fillRect(QRectF(imageRect.left(), cropRect.bottom(), imageRect.width(), imageRect.bottom() - cropRect.bottom()), shade);
    painter.fillRect(QRectF(imageRect.left(), cropRect.top(), cropRect.left() - imageRect.left(), cropRect.height()), shade);
    painter.fillRect(QRectF(cropRect.right(), cropRect.top(), imageRect.right() - cropRect.right(), cropRect.height()), shade);

    QPen borderPen(QColor(48, 220, 180), 2.0);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(cropRect.adjusted(1.0, 1.0, -1.0, -1.0));

    const qreal handleSize = 9.0;
    const QList<QPointF> handles = {
        cropRect.topLeft(),
        QPointF(cropRect.center().x(), cropRect.top()),
        cropRect.topRight(),
        QPointF(cropRect.left(), cropRect.center().y()),
        QPointF(cropRect.right(), cropRect.center().y()),
        cropRect.bottomLeft(),
        QPointF(cropRect.center().x(), cropRect.bottom()),
        cropRect.bottomRight()
    };

    painter.setPen(QPen(QColor(22, 80, 70), 1.0));
    painter.setBrush(QColor(236, 255, 250));

    for (const QPointF &handle : handles) {
        painter.drawRect(QRectF(handle.x() - handleSize / 2.0,
                                handle.y() - handleSize / 2.0,
                                handleSize,
                                handleSize));
    }
}

void CropImageView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_image.isNull()) {
        return QWidget::mousePressEvent(event);
    }

    m_dragMode = hitTest(event->position().toPoint());

    if (m_dragMode == DragNone) {
        return QWidget::mousePressEvent(event);
    }

    m_pressCropRect = m_cropRect;
    m_pressImagePoint = imagePointFromWidget(event->position().toPoint());
    event->accept();
}

void CropImageView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_image.isNull()) {
        return QWidget::mouseMoveEvent(event);
    }

    const QPoint widgetPoint = event->position().toPoint();

    if (m_dragMode == DragNone || !(event->buttons() & Qt::LeftButton)) {
        switch (hitTest(widgetPoint)) {
        case DragMove:
            setCursor(Qt::SizeAllCursor);
            break;
        case DragLeft:
        case DragRight:
            setCursor(Qt::SizeHorCursor);
            break;
        case DragTop:
        case DragBottom:
            setCursor(Qt::SizeVerCursor);
            break;
        case DragTopLeft:
        case DragBottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case DragTopRight:
        case DragBottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
        default:
            unsetCursor();
            break;
        }

        return QWidget::mouseMoveEvent(event);
    }

    const QPoint currentImagePoint = imagePointFromWidget(widgetPoint);
    const QPoint delta = currentImagePoint - m_pressImagePoint;
    QRect nextRect = m_pressCropRect;

    if (m_dragMode == DragMove) {
        nextRect.translate(delta);
    } else {
        int left = m_pressCropRect.left();
        int right = m_pressCropRect.right();
        int top = m_pressCropRect.top();
        int bottom = m_pressCropRect.bottom();

        if (changesLeft(m_dragMode)) {
            left += delta.x();
        }

        if (changesRight(m_dragMode)) {
            right += delta.x();
        }

        if (changesTop(m_dragMode)) {
            top += delta.y();
        }

        if (changesBottom(m_dragMode)) {
            bottom += delta.y();
        }

        const int minWidth = std::min(m_minCropSize, m_image.width());
        const int minHeight = std::min(m_minCropSize, m_image.height());

        left = std::clamp(left, 0, right - minWidth + 1);
        right = std::clamp(right, left + minWidth - 1, m_image.width() - 1);
        top = std::clamp(top, 0, bottom - minHeight + 1);
        bottom = std::clamp(bottom, top + minHeight - 1, m_image.height() - 1);

        nextRect = QRect(QPoint(left, top), QPoint(right, bottom));
    }

    const QRect clampedRect = clampCropRect(nextRect);

    if (clampedRect != m_cropRect) {
        m_cropRect = clampedRect;
        emit cropRectChanged(m_cropRect);
        update();
    }

    event->accept();
}

void CropImageView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragMode = DragNone;
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

QRectF CropImageView::imageDrawRect() const
{
    if (m_image.isNull() || width() <= 0 || height() <= 0) {
        return QRectF();
    }

    QSizeF scaledSize = m_image.size();
    scaledSize.scale(size(), Qt::KeepAspectRatio);

    return QRectF((width() - scaledSize.width()) / 2.0,
                  (height() - scaledSize.height()) / 2.0,
                  scaledSize.width(),
                  scaledSize.height());
}

QRectF CropImageView::cropWidgetRect() const
{
    if (m_image.isNull() || m_cropRect.isNull()) {
        return QRectF();
    }

    const QRectF imageRect = imageDrawRect();
    const qreal xScale = imageRect.width() / m_image.width();
    const qreal yScale = imageRect.height() / m_image.height();

    return QRectF(imageRect.left() + m_cropRect.x() * xScale,
                  imageRect.top() + m_cropRect.y() * yScale,
                  m_cropRect.width() * xScale,
                  m_cropRect.height() * yScale);
}

QPoint CropImageView::imagePointFromWidget(const QPoint &widgetPoint) const
{
    if (m_image.isNull()) {
        return QPoint();
    }

    const QRectF imageRect = imageDrawRect();

    if (!imageRect.isValid()) {
        return QPoint();
    }

    const qreal xScale = m_image.width() / imageRect.width();
    const qreal yScale = m_image.height() / imageRect.height();
    const int x = std::clamp(static_cast<int>((widgetPoint.x() - imageRect.left()) * xScale), 0, m_image.width() - 1);
    const int y = std::clamp(static_cast<int>((widgetPoint.y() - imageRect.top()) * yScale), 0, m_image.height() - 1);

    return QPoint(x, y);
}

QRect CropImageView::clampCropRect(const QRect &cropRect) const
{
    if (m_image.isNull()) {
        return cropRect;
    }

    QRect normalized = cropRect.normalized();
    const int minWidth = std::min(m_minCropSize, m_image.width());
    const int minHeight = std::min(m_minCropSize, m_image.height());
    const int cropWidth = std::clamp(normalized.width(), minWidth, m_image.width());
    const int cropHeight = std::clamp(normalized.height(), minHeight, m_image.height());
    const int x = std::clamp(normalized.x(), 0, m_image.width() - cropWidth);
    const int y = std::clamp(normalized.y(), 0, m_image.height() - cropHeight);

    return QRect(x, y, cropWidth, cropHeight);
}

CropImageView::DragMode CropImageView::hitTest(const QPoint &widgetPoint) const
{
    if (m_image.isNull()) {
        return DragNone;
    }

    const QRectF cropRect = cropWidgetRect();

    if (!cropRect.adjusted(-8.0, -8.0, 8.0, 8.0).contains(widgetPoint)) {
        return DragNone;
    }

    const bool nearLeft = std::abs(widgetPoint.x() - cropRect.left()) <= 8.0;
    const bool nearRight = std::abs(widgetPoint.x() - cropRect.right()) <= 8.0;
    const bool nearTop = std::abs(widgetPoint.y() - cropRect.top()) <= 8.0;
    const bool nearBottom = std::abs(widgetPoint.y() - cropRect.bottom()) <= 8.0;

    if (nearLeft && nearTop) {
        return DragTopLeft;
    }

    if (nearRight && nearTop) {
        return DragTopRight;
    }

    if (nearLeft && nearBottom) {
        return DragBottomLeft;
    }

    if (nearRight && nearBottom) {
        return DragBottomRight;
    }

    if (nearLeft) {
        return DragLeft;
    }

    if (nearRight) {
        return DragRight;
    }

    if (nearTop) {
        return DragTop;
    }

    if (nearBottom) {
        return DragBottom;
    }

    return cropRect.contains(widgetPoint) ? DragMove : DragNone;
}

bool CropImageView::changesLeft(DragMode mode)
{
    return mode == DragLeft || mode == DragTopLeft || mode == DragBottomLeft;
}

bool CropImageView::changesRight(DragMode mode)
{
    return mode == DragRight || mode == DragTopRight || mode == DragBottomRight;
}

bool CropImageView::changesTop(DragMode mode)
{
    return mode == DragTop || mode == DragTopLeft || mode == DragTopRight;
}

bool CropImageView::changesBottom(DragMode mode)
{
    return mode == DragBottom || mode == DragBottomLeft || mode == DragBottomRight;
}

CropReviewDialog::CropReviewDialog(const QList<CropPreviewItem> &items, QWidget *parent)
    : QDialog(parent)
    , m_items(items)
{
    setWindowTitle("Review Model Crops");
    resize(1180, 760);

    auto *rootLayout = new QVBoxLayout(this);
    m_headerLabel = new QLabel(this);
    m_headerLabel->setAlignment(Qt::AlignCenter);
    m_headerLabel->setStyleSheet("font-size: 16px; font-weight: 600;");
    rootLayout->addWidget(m_headerLabel);

    m_imageView = new CropImageView(this);
    rootLayout->addWidget(m_imageView, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    rootLayout->addWidget(m_statusLabel);

    auto *controlLayout = new QGridLayout();
    m_xSpinBox = new QSpinBox(this);
    m_ySpinBox = new QSpinBox(this);
    m_widthSpinBox = new QSpinBox(this);
    m_heightSpinBox = new QSpinBox(this);

    controlLayout->addWidget(new QLabel("X", this), 0, 0);
    controlLayout->addWidget(m_xSpinBox, 0, 1);
    controlLayout->addWidget(new QLabel("Y", this), 0, 2);
    controlLayout->addWidget(m_ySpinBox, 0, 3);
    controlLayout->addWidget(new QLabel("W", this), 0, 4);
    controlLayout->addWidget(m_widthSpinBox, 0, 5);
    controlLayout->addWidget(new QLabel("H", this), 0, 6);
    controlLayout->addWidget(m_heightSpinBox, 0, 7);
    rootLayout->addLayout(controlLayout);

    auto *buttonLayout = new QHBoxLayout();
    m_previousButton = new QPushButton("Previous", this);
    m_nextButton = new QPushButton("Next", this);
    auto *resetButton = new QPushButton("Reset Crop", this);
    m_exportButton = new QPushButton("Export Crops...", this);
    m_exportButton->setEnabled(!m_items.isEmpty());

    buttonLayout->addWidget(m_previousButton);
    buttonLayout->addWidget(m_nextButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(resetButton);
    buttonLayout->addWidget(m_exportButton);
    rootLayout->addLayout(buttonLayout);

    connect(m_imageView, &CropImageView::cropRectChanged, this, &CropReviewDialog::updateCurrentCropFromView);
    connect(m_xSpinBox, qOverload<int>(&QSpinBox::valueChanged), this, &CropReviewDialog::updateCurrentCropFromSpinBoxes);
    connect(m_ySpinBox, qOverload<int>(&QSpinBox::valueChanged), this, &CropReviewDialog::updateCurrentCropFromSpinBoxes);
    connect(m_widthSpinBox, qOverload<int>(&QSpinBox::valueChanged), this, &CropReviewDialog::updateCurrentCropFromSpinBoxes);
    connect(m_heightSpinBox, qOverload<int>(&QSpinBox::valueChanged), this, &CropReviewDialog::updateCurrentCropFromSpinBoxes);
    connect(m_previousButton, &QPushButton::clicked, this, &CropReviewDialog::goPrevious);
    connect(m_nextButton, &QPushButton::clicked, this, &CropReviewDialog::goNext);
    connect(resetButton, &QPushButton::clicked, this, &CropReviewDialog::resetCurrentCrop);
    connect(m_exportButton, &QPushButton::clicked, this, &CropReviewDialog::exportCrops);

    auto *previousShortcut = new QShortcut(QKeySequence(Qt::Key_Left), this);
    auto *nextShortcut = new QShortcut(QKeySequence(Qt::Key_Right), this);
    previousShortcut->setContext(Qt::WindowShortcut);
    nextShortcut->setContext(Qt::WindowShortcut);
    connect(previousShortcut, &QShortcut::activated, this, &CropReviewDialog::goPrevious);
    connect(nextShortcut, &QShortcut::activated, this, &CropReviewDialog::goNext);

    if (!m_items.isEmpty()) {
        setCurrentIndex(0);
    }
}

void CropReviewDialog::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }

    m_currentIndex = index;

    loadCurrentImage();
    refreshNavigation();
}

void CropReviewDialog::updateCurrentCropFromView(const QRect &cropRect)
{
    if (m_currentIndex < 0 || m_currentIndex >= m_items.size() || m_updatingControls) {
        return;
    }

    m_items[m_currentIndex].cropRect = clampedRectForCurrentImage(cropRect);
    refreshControls();
    refreshNavigation();
}

void CropReviewDialog::updateCurrentCropFromSpinBoxes()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_items.size() || m_updatingControls) {
        return;
    }

    QRect cropRect(m_xSpinBox->value(),
                   m_ySpinBox->value(),
                   m_widthSpinBox->value(),
                   m_heightSpinBox->value());
    cropRect = clampedRectForCurrentImage(cropRect);
    m_items[m_currentIndex].cropRect = cropRect;
    m_imageView->setCropRect(cropRect);
    refreshControls();
    refreshNavigation();
}

void CropReviewDialog::resetCurrentCrop()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_items.size()) {
        return;
    }

    m_items[m_currentIndex].cropRect = m_items[m_currentIndex].originalCropRect;
    m_imageView->setCropRect(m_items[m_currentIndex].cropRect);
    refreshControls();
    refreshNavigation();
}

void CropReviewDialog::goPrevious()
{
    if (m_currentIndex > 0) {
        setCurrentIndex(m_currentIndex - 1);
    }
}

void CropReviewDialog::goNext()
{
    if (m_currentIndex + 1 < m_items.size()) {
        setCurrentIndex(m_currentIndex + 1);
    }
}

void CropReviewDialog::exportCrops()
{
    if (m_items.isEmpty()) {
        QMessageBox::warning(this, "No Crops", "There are no crops to export.");
        return;
    }

    const QString destinationFolder = QFileDialog::getExistingDirectory(this, "Select Crop Export Folder");

    if (destinationFolder.isEmpty()) {
        return;
    }

    QProgressDialog progress("Exporting crops...", "Cancel", 0, m_items.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    int successCount = 0;
    int failCount = 0;

    for (int i = 0; i < m_items.size(); ++i) {
        progress.setValue(i);
        QApplication::processEvents();

        if (progress.wasCanceled()) {
            break;
        }

        const CropPreviewItem &item = m_items[i];
        cv::Mat image = cv::imread(item.imagePath.toStdString(), cv::IMREAD_UNCHANGED);

        if (image.empty()) {
            failCount++;
            continue;
        }

        const QRect cropRect = QRect(item.cropRect.x(),
                                     item.cropRect.y(),
                                     item.cropRect.width(),
                                     item.cropRect.height());
        cv::Rect cvCropRect(cropRect.x(), cropRect.y(), cropRect.width(), cropRect.height());
        cvCropRect &= cv::Rect(0, 0, image.cols, image.rows);

        if (cvCropRect.empty()) {
            failCount++;
            continue;
        }

        const QFileInfo fileInfo(item.imagePath);
        const QString outputPath = QDir(destinationFolder).filePath(fileInfo.completeBaseName() + "_crop.png");
        const cv::Mat cropped = image(cvCropRect).clone();

        if (cv::imwrite(outputPath.toStdString(), cropped)) {
            successCount++;
        } else {
            failCount++;
        }
    }

    progress.setValue(m_items.size());

    QMessageBox::information(this,
                             "Crop Export Complete",
                             QString("Exported: %1\nFailed: %2").arg(successCount).arg(failCount));
}

void CropReviewDialog::loadCurrentImage()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_items.size()) {
        return;
    }

    cv::Mat image = cv::imread(m_items[m_currentIndex].imagePath.toStdString(), cv::IMREAD_UNCHANGED);
    QImage displayImage = matToDisplayImage(image);

    if (displayImage.isNull()) {
        m_imageView->setImage(QImage());
        m_statusLabel->setText("Image could not be loaded for review.");
        return;
    }

    m_imageView->setImage(displayImage);
    m_imageView->setCropRect(m_items[m_currentIndex].cropRect);
    refreshControls();
    refreshNavigation();
}

void CropReviewDialog::refreshControls()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_items.size()) {
        return;
    }

    const QSize imageSize = m_items[m_currentIndex].imageSize;
    const QRect cropRect = clampedRectForCurrentImage(m_items[m_currentIndex].cropRect);

    m_updatingControls = true;
    m_xSpinBox->setRange(0, std::max(0, imageSize.width() - 1));
    m_ySpinBox->setRange(0, std::max(0, imageSize.height() - 1));
    m_widthSpinBox->setRange(1, std::max(1, imageSize.width()));
    m_heightSpinBox->setRange(1, std::max(1, imageSize.height()));
    m_xSpinBox->setValue(cropRect.x());
    m_ySpinBox->setValue(cropRect.y());
    m_widthSpinBox->setValue(cropRect.width());
    m_heightSpinBox->setValue(cropRect.height());
    m_updatingControls = false;
}

void CropReviewDialog::refreshNavigation()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_items.size()) {
        return;
    }

    const CropPreviewItem &item = m_items[m_currentIndex];
    const QFileInfo fileInfo(item.imagePath);
    const QRect cropRect = item.cropRect;

    if (m_headerLabel != nullptr) {
        m_headerLabel->setText(QString("%1  (%2/%3)")
                                   .arg(fileInfo.fileName())
                                   .arg(m_currentIndex + 1)
                                   .arg(m_items.size()));
    }

    if (m_statusLabel != nullptr) {
        m_statusLabel->setText(QString("%1 x %2 image  |  %3 x %4 crop")
                                   .arg(item.imageSize.width())
                                   .arg(item.imageSize.height())
                                   .arg(cropRect.width())
                                   .arg(cropRect.height()));
    }

    if (m_previousButton != nullptr) {
        m_previousButton->setEnabled(m_currentIndex > 0);
    }

    if (m_nextButton != nullptr) {
        m_nextButton->setEnabled(m_currentIndex + 1 < m_items.size());
    }

    if (m_exportButton != nullptr) {
        m_exportButton->setEnabled(!m_items.isEmpty());
    }
}

QRect CropReviewDialog::clampedRectForCurrentImage(const QRect &cropRect) const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_items.size()) {
        return cropRect;
    }

    const QSize imageSize = m_items[m_currentIndex].imageSize;

    if (imageSize.isEmpty()) {
        return cropRect;
    }

    const QRect normalized = cropRect.normalized();
    const int width = std::clamp(normalized.width(), 1, imageSize.width());
    const int height = std::clamp(normalized.height(), 1, imageSize.height());
    const int x = std::clamp(normalized.x(), 0, imageSize.width() - width);
    const int y = std::clamp(normalized.y(), 0, imageSize.height() - height);

    return QRect(x, y, width, height);
}

QImage CropReviewDialog::matToDisplayImage(const cv::Mat &mat) const
{
    if (mat.empty()) {
        return QImage();
    }

    cv::Mat display;

    if (mat.depth() == CV_8U) {
        display = mat;
    } else {
        double minValue = 0.0;
        double maxValue = 0.0;
        cv::minMaxLoc(mat.reshape(1), &minValue, &maxValue);

        if (maxValue > minValue) {
            mat.convertTo(display, CV_8U, 255.0 / (maxValue - minValue), -minValue * 255.0 / (maxValue - minValue));
        } else {
            mat.convertTo(display, CV_8U);
        }
    }

    if (display.channels() == 1) {
        QImage image(display.data,
                     display.cols,
                     display.rows,
                     static_cast<int>(display.step),
                     QImage::Format_Grayscale8);
        return image.copy();
    }

    if (display.channels() == 3) {
        cv::Mat rgb;
        cv::cvtColor(display, rgb, cv::COLOR_BGR2RGB);
        QImage image(rgb.data,
                     rgb.cols,
                     rgb.rows,
                     static_cast<int>(rgb.step),
                     QImage::Format_RGB888);
        return image.copy();
    }

    if (display.channels() == 4) {
        cv::Mat rgba;
        cv::cvtColor(display, rgba, cv::COLOR_BGRA2RGBA);
        QImage image(rgba.data,
                     rgba.cols,
                     rgba.rows,
                     static_cast<int>(rgba.step),
                     QImage::Format_RGBA8888);
        return image.copy();
    }

    return QImage();
}
