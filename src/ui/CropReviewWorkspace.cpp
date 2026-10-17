#include "CropReviewWorkspace.h"
#include "DesktopIcons.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <QApplication>
#include <QAction>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QGridLayout>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QImageReader>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressDialog>
#include <QResizeEvent>
#include <QPushButton>
#include <QShortcut>
#include <QSizePolicy>
#include <QSpinBox>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QPromise>
#include <QtConcurrent/QtConcurrentRun>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <vector>

CropImageView::CropImageView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void CropImageView::setImage(const QImage &image)
{
    m_image = image;
    m_dragMode = DragNone;
    m_zoom = 1.0;
    m_panOffset = QPointF();
    m_panning = false;

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

QImage CropImageView::cropPreview(const QSize &size) const
{
    if (m_image.isNull() || m_cropRect.isEmpty() || size.isEmpty()) return {};
    QImage preview(size, QImage::Format_RGB32);
    preview.fill(QColor("#25282b"));
    const QSize fitted = m_cropRect.size().scaled(size, Qt::KeepAspectRatio);
    QPainter painter(&preview);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRect(QPoint((size.width() - fitted.width()) / 2, (size.height() - fitted.height()) / 2), fitted),
                      m_image, m_cropRect);
    return preview;
}

double CropImageView::zoomFactor() const
{
    return m_image.isNull() ? 1.0 : imageDrawRect().width() / m_image.width();
}

void CropImageView::fitImage()
{
    m_zoom = 1.0;
    m_panOffset = QPointF();
    update();
    emit zoomChanged(zoomFactor());
}

void CropImageView::actualSize()
{
    if (m_image.isNull()) return;
    m_zoom = 1.0;
    m_zoom = 1.0 / zoomFactor();
    m_panOffset = QPointF();
    update();
    emit zoomChanged(zoomFactor());
}

QSize CropImageView::minimumSizeHint() const
{
    return QSize(520, 360);
}

void CropImageView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor("#25282b"));
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
    if (event->button() == Qt::MiddleButton && !m_image.isNull()) {
        m_panning = true;
        m_panStart = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
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
    if (m_panning && (event->buttons() & Qt::MiddleButton)) {
        m_panOffset += event->position() - m_panStart;
        m_panStart = event->position();
        update();
        event->accept();
        return;
    }
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
    if (event->button() == Qt::MiddleButton) {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        m_dragMode = DragNone;
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void CropImageView::wheelEvent(QWheelEvent *event)
{
    if (m_image.isNull()) return;
    const QRectF oldRect = imageDrawRect();
    const QPointF relative((event->position().x() - oldRect.left()) / oldRect.width(),
                           (event->position().y() - oldRect.top()) / oldRect.height());
    m_zoom = std::clamp(m_zoom * std::pow(1.15, event->angleDelta().y() / 120.0), 0.05, 64.0);
    const QRectF nextRect = imageDrawRect();
    m_panOffset += event->position() - QPointF(nextRect.left() + relative.x() * nextRect.width(),
                                               nextRect.top() + relative.y() * nextRect.height());
    update();
    emit zoomChanged(zoomFactor());
    event->accept();
}

void CropImageView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    emit zoomChanged(zoomFactor());
}

QRectF CropImageView::imageDrawRect() const
{
    if (m_image.isNull() || width() <= 0 || height() <= 0) {
        return QRectF();
    }

    QSizeF scaledSize = m_image.size();
    scaledSize.scale(size(), Qt::KeepAspectRatio);
    scaledSize *= m_zoom;

    return QRectF((width() - scaledSize.width()) / 2.0 + m_panOffset.x(),
                  (height() - scaledSize.height()) / 2.0 + m_panOffset.y(),
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

CropReviewWorkspace::CropReviewWorkspace(const CropReviewSession &session, QWidget *parent, const QString &sessionPath)
    : QWidget(parent)
{
    setObjectName("cropReviewWorkspace");
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto *toolbar = new QToolBar(this);
    toolbar->setObjectName("reviewToolbar");
    toolbar->setIconSize(QSize(18, 18));
    m_newAction = toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogNewFolder), "New review...");
    m_newAction->setObjectName("actionNewReview");
    m_openAction = toolbar->addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), "Open session...");
    m_openAction->setObjectName("actionOpenReview");
    toolbar->addSeparator();
    m_saveAction = toolbar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), "Save session");
    m_saveAction->setObjectName("actionSaveReview");
    m_saveAsAction = new QAction("Save session as...", this);
    m_saveAsAction->setIcon(DesktopIcons::icon(DesktopIcons::Symbol::Copy));
    toolbar->addSeparator();
    auto *openSource = toolbar->addAction(style()->standardIcon(QStyle::SP_FileIcon), "Open source in preparation");
    toolbar->addSeparator();
    auto *fit = toolbar->addAction(style()->standardIcon(QStyle::SP_TitleBarMaxButton), "Fit image");
    auto *actual = toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), "Actual size");
    actual->setIconText("1:1");
    actual->setToolTip("One image pixel per display pixel");
    qobject_cast<QToolButton *>(toolbar->widgetForAction(actual))->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    m_exportAction = toolbar->addAction(DesktopIcons::icon(DesktopIcons::Symbol::Export), "Export approved...");
    m_exportAction->setObjectName("actionExportApproved");
    for (QAction *action : {m_newAction, m_exportAction}) {
        if (auto *button = qobject_cast<QToolButton *>(toolbar->widgetForAction(action))) {
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        }
    }
    root->addWidget(toolbar);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName("reviewSplitter");
    splitter->setChildrenCollapsible(false);
    m_content = splitter;
    auto *browser = new QWidget(splitter);
    browser->setObjectName("reviewBrowser");
    browser->setMinimumWidth(170);
    auto *browserLayout = new QVBoxLayout(browser);
    browserLayout->setContentsMargins(10, 8, 10, 8);
    auto *browserTitle = new QLabel("Review queue", browser);
    browserTitle->setObjectName("panelTitle");
    browserLayout->addWidget(browserTitle);
    m_filter = new QComboBox(browser);
    m_filter->setObjectName("reviewFilter");
    m_filter->addItems({"All images", "Pending", "Approved", "Rejected", "Needs review", "Prediction warnings"});
    browserLayout->addWidget(m_filter);
    m_imageList = new QListWidget(browser);
    m_imageList->setObjectName("reviewImageList");
    m_imageList->setIconSize(QSize(64, 48));
    m_imageList->setTextElideMode(Qt::ElideMiddle);
    browserLayout->addWidget(m_imageList, 1);
    m_summaryLabel = new QLabel(browser);
    m_summaryLabel->setObjectName("reviewSummary");
    m_summaryLabel->setWordWrap(true);
    browserLayout->addWidget(m_summaryLabel);

    auto *canvas = new QWidget(splitter);
    auto *canvasLayout = new QVBoxLayout(canvas);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->setSpacing(0);
    auto *navigation = new QWidget(canvas);
    auto *navigationLayout = new QHBoxLayout(navigation);
    navigationLayout->setContentsMargins(8, 5, 8, 5);
    m_headerLabel = new QLabel("Crop review", navigation);
    m_headerLabel->setTextFormat(Qt::PlainText);
    m_headerLabel->setObjectName("imageHeader");
    m_headerLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_previousButton = new QPushButton(style()->standardIcon(QStyle::SP_ArrowBack), QString(), navigation);
    m_previousButton->setObjectName("previousCropButton");
    m_nextButton = new QPushButton(style()->standardIcon(QStyle::SP_ArrowForward), QString(), navigation);
    m_nextButton->setObjectName("nextCropButton");
    m_previousButton->setFixedSize(32, 32);
    m_nextButton->setFixedSize(32, 32);
    m_previousButton->setProperty("iconOnly", true);
    m_nextButton->setProperty("iconOnly", true);
    m_previousButton->setToolTip("Previous visible image (Alt+Left)");
    m_nextButton->setToolTip("Next visible image (Alt+Right)");
    navigationLayout->addWidget(m_headerLabel, 1);
    navigationLayout->addWidget(m_previousButton);
    navigationLayout->addWidget(m_nextButton);
    canvasLayout->addWidget(navigation);
    m_imageView = new CropImageView(canvas);
    m_imageView->setObjectName("reviewImageView");
    m_imageView->setToolTip("Scroll to zoom. Drag with the middle mouse button to pan.");
    canvasLayout->addWidget(m_imageView, 1);

    auto *inspectorColumn = new QWidget(splitter);
    inspectorColumn->setObjectName("reviewInspector");
    inspectorColumn->setMinimumWidth(250);
    auto *columnLayout = new QVBoxLayout(inspectorColumn);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(0);
    auto *inspectorTitle = new QLabel("Crop properties", inspectorColumn);
    inspectorTitle->setObjectName("panelTitle");
    inspectorTitle->setContentsMargins(14, 8, 14, 0);
    columnLayout->addWidget(inspectorTitle);
    auto *scroll = new QScrollArea(inspectorColumn);
    scroll->setWidgetResizable(true);
    auto *inspector = new QWidget(scroll);
    inspector->setObjectName("reviewInspector");
    auto *inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(14, 8, 14, 12);
    inspectorLayout->setSpacing(8);
    const auto heading = [inspector, inspectorLayout](const QString &text, bool title = false) {
        auto *label = new QLabel(text, inspector);
        label->setObjectName(title ? "panelTitle" : "sectionTitle");
        inspectorLayout->addWidget(label);
    };
    m_cropPreview = new QLabel(inspector);
    m_cropPreview->setObjectName("cropPreview");
    m_cropPreview->setFixedHeight(72);
    m_cropPreview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_cropPreview->setAlignment(Qt::AlignCenter);
    m_cropPreview->setStyleSheet("background: #25282b; color: #929b9f;");
    inspectorLayout->addWidget(m_cropPreview);
    m_statusLabel = new QLabel(inspector);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setObjectName("cropMetadata");
    inspectorLayout->addWidget(m_statusLabel);
    m_predictionLabel = new QLabel(inspector);
    m_predictionLabel->setObjectName("predictionStatus");
    m_predictionLabel->setTextFormat(Qt::PlainText);
    m_predictionLabel->setWordWrap(true);
    inspectorLayout->addWidget(m_predictionLabel);
    m_modelLabel = new QLabel(inspector);
    m_modelLabel->setObjectName("modelMetadata");
    m_modelLabel->setStyleSheet("color: #657078; font-size: 11px;");
    m_modelLabel->setWordWrap(true);
    inspectorLayout->addWidget(m_modelLabel);
    heading("Bounds");
    auto *bounds = new QGridLayout();
    m_xSpinBox = new QSpinBox(inspector);
    m_ySpinBox = new QSpinBox(inspector);
    m_widthSpinBox = new QSpinBox(inspector);
    m_heightSpinBox = new QSpinBox(inspector);
    m_xSpinBox->setObjectName("cropX");
    m_ySpinBox->setObjectName("cropY");
    m_widthSpinBox->setObjectName("cropWidth");
    m_heightSpinBox->setObjectName("cropHeight");
    bounds->addWidget(new QLabel("X", inspector), 0, 0);
    bounds->addWidget(new QLabel("Y", inspector), 0, 1);
    bounds->addWidget(m_xSpinBox, 1, 0);
    bounds->addWidget(m_ySpinBox, 1, 1);
    bounds->addWidget(new QLabel("Width", inspector), 2, 0);
    bounds->addWidget(new QLabel("Height", inspector), 2, 1);
    bounds->addWidget(m_widthSpinBox, 3, 0);
    bounds->addWidget(m_heightSpinBox, 3, 1);
    inspectorLayout->addLayout(bounds);
    m_resetButton = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), "Reset to suggestion", inspector);
    m_resetButton->setToolTip("Restore the model's crop and clear approval");
    inspectorLayout->addWidget(m_resetButton);
    inspectorLayout->addStretch();
    scroll->setWidget(inspector);
    columnLayout->addWidget(scroll, 1);
    auto *footer = new QWidget(inspectorColumn);
    auto *footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(14, 4, 14, 10);
    footerLayout->setSpacing(7);
    auto *decisionTitle = new QLabel("Decision", footer);
    decisionTitle->setObjectName("sectionTitle");
    footerLayout->addWidget(decisionTitle);
    auto *decisions = new QGridLayout();
    m_approveButton = new QPushButton(DesktopIcons::icon(DesktopIcons::Symbol::Check, true), "Approve", inspector);
    m_approveButton->setObjectName("approveCropButton");
    m_approveButton->setProperty("primary", true);
    m_rejectButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogCancelButton), "Reject", inspector);
    m_rejectButton->setObjectName("rejectCropButton");
    m_flagButton = new QPushButton(DesktopIcons::icon(DesktopIcons::Symbol::Flag), "Flag", inspector);
    m_flagButton->setObjectName("flagCropButton");
    m_flagButton->setToolTip("Mark this image as Needs review");
    m_pendingButton = new QPushButton(DesktopIcons::icon(DesktopIcons::Symbol::Clear), "Clear decision", inspector);
    m_pendingButton->setObjectName("clearDecisionButton");
    decisions->addWidget(m_approveButton, 0, 0, 1, 2);
    decisions->addWidget(m_rejectButton, 1, 0);
    decisions->addWidget(m_flagButton, 1, 1);
    decisions->addWidget(m_pendingButton, 2, 0, 1, 2);
    footerLayout->addLayout(decisions);
    m_autoAdvance = new QCheckBox("Advance after decision", footer);
    m_autoAdvance->setObjectName("advanceAfterDecision");
    footerLayout->addWidget(m_autoAdvance);
    columnLayout->addWidget(footer);
    splitter->addWidget(browser);
    splitter->addWidget(canvas);
    splitter->addWidget(inspectorColumn);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({220, 760, 272});
    root->addWidget(splitter, 1);

    connect(m_imageView, &CropImageView::cropRectChanged, this, &CropReviewWorkspace::updateCurrentCropFromView);
    for (QSpinBox *spin : {m_xSpinBox, m_ySpinBox, m_widthSpinBox, m_heightSpinBox}) {
        connect(spin, &QSpinBox::valueChanged, this, &CropReviewWorkspace::updateCurrentCropFromSpinBoxes);
    }
    connect(m_previousButton, &QPushButton::clicked, this, &CropReviewWorkspace::goPrevious);
    connect(m_nextButton, &QPushButton::clicked, this, &CropReviewWorkspace::goNext);
    connect(m_resetButton, &QPushButton::clicked, this, &CropReviewWorkspace::resetCurrentCrop);
    connect(m_exportAction, &QAction::triggered, this, &CropReviewWorkspace::exportCrops);
    connect(m_saveAction, &QAction::triggered, this, &CropReviewWorkspace::saveSession);
    connect(m_saveAsAction, &QAction::triggered, this, &CropReviewWorkspace::saveSessionAs);
    connect(m_newAction, &QAction::triggered, this, &CropReviewWorkspace::newReviewRequested);
    connect(m_openAction, &QAction::triggered, this, &CropReviewWorkspace::openSessionRequested);
    const auto openCurrentSource = [this] {
        if (m_currentIndex >= 0 && m_imageAvailable) emit openSourceRequested(m_items[m_currentIndex].imagePath);
    };
    connect(openSource, &QAction::triggered, this, openCurrentSource);
    connect(fit, &QAction::triggered, m_imageView, &CropImageView::fitImage);
    connect(actual, &QAction::triggered, m_imageView, &CropImageView::actualSize);
    connect(this, &CropReviewWorkspace::stateChanged, this, [this, openSource, fit, actual] {
        const bool enabled = m_currentIndex >= 0 && m_imageAvailable && !m_exporting && !m_externalBusy;
        for (auto *action : {openSource, fit, actual}) action->setEnabled(enabled);
    });
    connect(m_filter, &QComboBox::currentIndexChanged, this, &CropReviewWorkspace::applyFilter);
    connect(m_imageList, &QListWidget::currentRowChanged, this, &CropReviewWorkspace::setCurrentIndex);
    connect(m_approveButton, &QPushButton::clicked, this, [this] { setReviewState(CropReviewState::Approved); });
    connect(m_rejectButton, &QPushButton::clicked, this, [this] { setReviewState(CropReviewState::Rejected); });
    connect(m_flagButton, &QPushButton::clicked, this, [this] { setReviewState(CropReviewState::NeedsReview); });
    connect(m_pendingButton, &QPushButton::clicked, this, [this] { setReviewState(CropReviewState::Pending); });
    auto *previous = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this);
    auto *next = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this);
    previous->setContext(Qt::WidgetWithChildrenShortcut);
    next->setContext(Qt::WidgetWithChildrenShortcut);
    connect(previous, &QShortcut::activated, this, &CropReviewWorkspace::goPrevious);
    connect(next, &QShortcut::activated, this, &CropReviewWorkspace::goNext);
    for (QPushButton *button : findChildren<QPushButton *>()) button->setAutoDefault(false);
    setSession(session, sessionPath);
}

CropReviewWorkspace::~CropReviewWorkspace()
{
    if (m_exportCancel) m_exportCancel->store(true);
    if (m_exportWatcher) m_exportWatcher->waitForFinished();
}

void CropReviewWorkspace::setSession(const CropReviewSession &session, const QString &path)
{
    if (m_exporting) return;
    ++m_thumbnailGeneration;
    m_items = session.items;
    m_modelPath = session.modelPath;
    m_sessionPath = path;
    m_cropMarginPixels = session.cropMarginPixels;
    m_predictionThreshold = session.predictionThreshold;
    m_dirty = false;
    m_currentIndex = -1;
    m_imageAvailable = false;
    m_imageView->setImage(QImage());
    const QSignalBlocker listBlocker(m_imageList), filterBlocker(m_filter);
    m_filter->setCurrentIndex(0);
    m_imageList->clear();
    for (int index = 0; index < m_items.size(); ++index) {
        auto *entry = new QListWidgetItem(style()->standardIcon(QStyle::SP_FileIcon), QString(), m_imageList);
        entry->setSizeHint(QSize(180, 68));
        refreshListItem(index);
    }
    if (!m_items.isEmpty()) {
        setCurrentIndex(std::clamp(session.currentIndex, 0, static_cast<int>(m_items.size()) - 1));
        const int generation = m_thumbnailGeneration;
        QTimer::singleShot(0, this, [this, generation] { loadThumbnail(0, generation); });
    }
    refreshNavigation();
}

bool CropReviewWorkspace::isBusy() const { return m_exporting; }
bool CropReviewWorkspace::hasSession() const { return !m_items.isEmpty(); }
bool CropReviewWorkspace::isDirty() const { return m_dirty; }
QString CropReviewWorkspace::sessionPath() const { return m_sessionPath; }
QAction *CropReviewWorkspace::saveAction() const { return m_saveAction; }
QAction *CropReviewWorkspace::saveAsAction() const { return m_saveAsAction; }
QAction *CropReviewWorkspace::exportAction() const { return m_exportAction; }
QAction *CropReviewWorkspace::newAction() const { return m_newAction; }
QAction *CropReviewWorkspace::openAction() const { return m_openAction; }

void CropReviewWorkspace::setExternalBusy(bool busy)
{
    if (m_externalBusy == busy) return;
    m_externalBusy = busy;
    m_content->setEnabled(!m_externalBusy && !m_exporting);
    refreshNavigation();
}

void CropReviewWorkspace::setCurrentIndex(int index)
{
    if (m_exporting || index < 0 || index >= m_items.size() || m_imageList->item(index)->isHidden()) {
        return;
    }

    m_currentIndex = index;
    const QSignalBlocker blocker(m_imageList);
    m_imageList->setCurrentRow(index);

    loadCurrentImage();
    refreshNavigation();
}

void CropReviewWorkspace::updateCurrentCropFromView(const QRect &cropRect)
{
    if (m_exporting || m_currentIndex < 0 || m_currentIndex >= m_items.size() || m_updatingControls) {
        return;
    }

    if (!m_imageAvailable) return;
    const QRect nextRect = clampedRectForCurrentImage(cropRect);
    if (nextRect == m_items[m_currentIndex].cropRect) return;
    m_items[m_currentIndex].cropRect = nextRect;
    markCropChanged();
    refreshControls();
    refreshNavigation();
    applyFilter();
}

void CropReviewWorkspace::updateCurrentCropFromSpinBoxes()
{
    if (m_exporting || m_currentIndex < 0 || m_currentIndex >= m_items.size() || m_updatingControls) {
        return;
    }

    QRect cropRect(m_xSpinBox->value(),
                   m_ySpinBox->value(),
                   m_widthSpinBox->value(),
                   m_heightSpinBox->value());
    cropRect = clampedRectForCurrentImage(cropRect);
    if (!m_imageAvailable || cropRect == m_items[m_currentIndex].cropRect) return;
    m_items[m_currentIndex].cropRect = cropRect;
    markCropChanged();
    m_imageView->setCropRect(cropRect);
    refreshControls();
    refreshNavigation();
    applyFilter();
}

void CropReviewWorkspace::resetCurrentCrop()
{
    if (m_exporting || !m_imageAvailable || m_currentIndex < 0 || m_currentIndex >= m_items.size()) {
        return;
    }

    m_items[m_currentIndex].cropRect = m_items[m_currentIndex].originalCropRect;
    markCropChanged();
    m_imageView->setCropRect(m_items[m_currentIndex].cropRect);
    refreshControls();
    refreshNavigation();
    applyFilter();
}

void CropReviewWorkspace::goPrevious()
{
    const int previous = adjacentVisibleIndex(-1);
    if (previous >= 0) setCurrentIndex(previous);
}

void CropReviewWorkspace::goNext()
{
    const int next = adjacentVisibleIndex(1);
    if (next >= 0) setCurrentIndex(next);
}

void CropReviewWorkspace::exportCrops()
{
    if (m_exporting || m_externalBusy || !m_exportAction->isEnabled()) return;
    const QString folder = QFileDialog::getExistingDirectory(this, "Export Approved Crops");
    if (folder.isEmpty()) return;
    m_exporting = true;
    m_content->setEnabled(false);
    m_exportCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_exportCancel;
    const CropReviewSession snapshot = currentSession();
    m_exportWatcher = new QFutureWatcher<CropExportResult>(this);
    auto *watcher = m_exportWatcher;
    connect(watcher, &QFutureWatcherBase::progressRangeChanged, this, [this](int minimum, int maximum) {
        emit jobProgress(minimum, maximum);
    });
    connect(watcher, &QFutureWatcherBase::progressValueChanged, this, [this, watcher](int value) {
        emit jobProgress(value, watcher->progressMaximum());
    });
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        const CropExportResult result = watcher->result();
        m_exporting = false;
        m_content->setEnabled(true);
        m_exportWatcher = nullptr;
        watcher->deleteLater();
        refreshNavigation();
        emit statusMessage(QString("%1: %2 exported, %3 failed. %4")
                               .arg(result.canceled ? "Export canceled" : "Export complete")
                               .arg(result.exported).arg(result.failed).arg(result.manifestPath));
        emit busyChanged(false);
        if (!result.errors.isEmpty() && !m_exportCancel->load() && !QApplication::activeModalWidget()) {
            QMessageBox message(QMessageBox::Warning, "Crop Export", "Some export files could not be written.",
                                QMessageBox::Ok, this);
            message.setDetailedText(result.errors.join('\n'));
            message.exec();
        }
    });
    refreshNavigation();
    emit busyChanged(true);
    emit statusMessage("Exporting approved crops...");
    watcher->setFuture(QtConcurrent::run([snapshot, folder, cancel](QPromise<CropExportResult> &promise) {
        promise.setProgressRange(0, static_cast<int>(snapshot.items.size()));
        const auto result = snapshot.exportApproved(folder, [&promise, cancel](int value, int) {
            promise.setProgressValue(value);
            return !cancel->load();
        });
        promise.addResult(result);
    }));
}

void CropReviewWorkspace::cancelExport()
{
    if (m_exportCancel) m_exportCancel->store(true);
}

void CropReviewWorkspace::loadCurrentImage()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_items.size()) {
        return;
    }

    cv::Mat image;
    try {
        QFile file(m_items[m_currentIndex].imagePath);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = file.readAll();
            image = cv::imdecode(std::vector<uchar>(bytes.begin(), bytes.end()), cv::IMREAD_UNCHANGED);
        }
    } catch (const cv::Exception &) {
    }
    QImage displayImage = matToDisplayImage(image);
    m_imageAvailable = !displayImage.isNull() && displayImage.size() == m_items[m_currentIndex].imageSize;
    m_imageView->setImage(displayImage);
    if (m_imageAvailable) m_imageView->setCropRect(m_items[m_currentIndex].cropRect);
    refreshControls();
    refreshNavigation();
}

void CropReviewWorkspace::refreshControls()
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

void CropReviewWorkspace::refreshNavigation()
{
    int approved = 0, pending = 0, rejected = 0, flagged = 0;
    for (const CropPreviewItem &item : m_items) {
        switch (item.reviewState) {
        case CropReviewState::Approved: ++approved; break;
        case CropReviewState::Rejected: ++rejected; break;
        case CropReviewState::NeedsReview: ++flagged; break;
        default: ++pending; break;
        }
    }
    m_summaryLabel->setText(QString("%1 images\n%2 approved | %3 pending\n%4 rejected | %5 flagged")
                               .arg(m_items.size()).arg(approved).arg(pending).arg(rejected).arg(flagged));
    const bool available = !m_exporting && !m_externalBusy;
    m_exportAction->setEnabled(approved > 0 && available);
    m_exportAction->setText(QString("Export approved (%1)...").arg(approved));
    m_saveAction->setEnabled(hasSession() && available);
    m_saveAction->setToolTip(m_sessionPath.isEmpty() ? "Save review session" : "Save " + m_sessionPath);
    m_saveAsAction->setEnabled(hasSession() && available);
    m_newAction->setEnabled(available);
    m_openAction->setEnabled(available);
    const bool hasImage = m_currentIndex >= 0 && m_currentIndex < m_items.size();
    for (QSpinBox *spin : {m_xSpinBox, m_ySpinBox, m_widthSpinBox, m_heightSpinBox}) {
        spin->setEnabled(hasImage && m_imageAvailable);
    }
    m_approveButton->setEnabled(hasImage && m_imageAvailable);
    m_rejectButton->setEnabled(hasImage);
    m_flagButton->setEnabled(hasImage);
    m_pendingButton->setEnabled(hasImage);
    m_resetButton->setEnabled(hasImage && m_imageAvailable);
    m_previousButton->setEnabled(adjacentVisibleIndex(-1) >= 0);
    m_nextButton->setEnabled(adjacentVisibleIndex(1) >= 0);
    m_modelLabel->setText(hasSession()
        ? QString("%1\nThreshold %2 | Margin %3 px").arg(QFileInfo(m_modelPath).fileName())
              .arg(m_predictionThreshold, 0, 'f', 2).arg(m_cropMarginPixels)
        : QString());
    m_modelLabel->setToolTip(m_modelPath);
    m_modelLabel->setVisible(hasSession());
    if (!hasImage) {
        m_headerLabel->setText(hasSession() ? "No matching images" : "Crop review");
        m_statusLabel->setText(hasSession() ? QString() : "No session");
        m_predictionLabel->clear();
        m_cropPreview->setText("No crop");
        emit stateChanged();
        return;
    }
    const CropPreviewItem &item = m_items[m_currentIndex];
    m_headerLabel->setText(QString("%1   (%2 / %3)").arg(QFileInfo(item.imagePath).fileName())
                               .arg(m_currentIndex + 1).arg(m_items.size()));
    m_headerLabel->setToolTip(item.imagePath);
    m_statusLabel->setText(QString("%1\nImage: %2 x %3 px\nCrop: %4 x %5 px")
                               .arg(cropReviewStateName(item.reviewState))
                               .arg(item.imageSize.width()).arg(item.imageSize.height())
                               .arg(item.cropRect.width()).arg(item.cropRect.height()));
    QString prediction = item.statusText.isEmpty()
        ? (item.predictionFound ? "Model crop ready." : "No confident prediction; full image selected.")
        : item.statusText;
    if (!m_imageAvailable) prediction = "Source image missing, unreadable or changed. " + prediction;
    m_predictionLabel->setText(prediction.size() > 240 ? prediction.left(237) + "..." : prediction);
    m_predictionLabel->setToolTip(prediction);
    m_predictionLabel->setVisible(!item.predictionFound || !m_imageAvailable);
    m_predictionLabel->setStyleSheet(!item.predictionFound || !m_imageAvailable
        ? "color: #914d16; background: #fff3df; padding: 7px; border-left: 2px solid #d99534;"
        : "color: #657078;");
    if (m_imageAvailable) {
        m_cropPreview->setPixmap(QPixmap::fromImage(m_imageView->cropPreview(QSize(200, 72))));
    } else {
        m_cropPreview->setText("Unavailable");
    }
    emit stateChanged();
}

void CropReviewWorkspace::markCropChanged()
{
    if (m_items[m_currentIndex].reviewState == CropReviewState::Approved) {
        m_items[m_currentIndex].reviewState = CropReviewState::Pending;
    }
    m_dirty = true;
    refreshListItem(m_currentIndex);
}

void CropReviewWorkspace::setReviewState(CropReviewState state)
{
    if (m_exporting || m_currentIndex < 0 || (state == CropReviewState::Approved && !m_imageAvailable)) return;
    const int next = adjacentVisibleIndex(1);
    if (m_items[m_currentIndex].reviewState != state) {
        m_items[m_currentIndex].reviewState = state;
        m_dirty = true;
        refreshListItem(m_currentIndex);
    }
    applyFilter();
    if (m_autoAdvance->isChecked() && next >= 0 && matchesFilter(next)) setCurrentIndex(next);
    refreshNavigation();
}

void CropReviewWorkspace::refreshListItem(int index)
{
    const CropPreviewItem &item = m_items[index];
    QListWidgetItem *entry = m_imageList->item(index);
    const QString state = item.reviewState == CropReviewState::NeedsReview ? "Flagged" : cropReviewStateName(item.reviewState);
    entry->setText(QFileInfo(item.imagePath).fileName() + '\n' + state
                   + (item.predictionFound ? "" : "  |  Warning"));
    entry->setToolTip(item.imagePath + '\n' + item.statusText);
    entry->setForeground(item.reviewState == CropReviewState::Rejected ? palette().color(QPalette::Disabled, QPalette::Text)
                                                                      : palette().color(QPalette::Text));
}

void CropReviewWorkspace::loadThumbnail(int index, int generation)
{
    if (generation != m_thumbnailGeneration || index >= m_items.size()) return;
    QImageReader reader(m_items[index].imagePath);
    const QSize originalSize = reader.size();
    if (originalSize.isValid()) reader.setScaledSize(originalSize.scaled(QSize(64, 48), Qt::KeepAspectRatio));
    const QImage image = reader.read();
    if (!image.isNull()) m_imageList->item(index)->setIcon(QPixmap::fromImage(image));
    QTimer::singleShot(0, this, [this, index, generation] { loadThumbnail(index + 1, generation); });
}

bool CropReviewWorkspace::matchesFilter(int index) const
{
    if (index < 0 || index >= m_items.size()) return false;
    switch (m_filter->currentIndex()) {
    case 1: return m_items[index].reviewState == CropReviewState::Pending;
    case 2: return m_items[index].reviewState == CropReviewState::Approved;
    case 3: return m_items[index].reviewState == CropReviewState::Rejected;
    case 4: return m_items[index].reviewState == CropReviewState::NeedsReview;
    case 5: return !m_items[index].predictionFound;
    default: return true;
    }
}

void CropReviewWorkspace::applyFilter()
{
    if (m_exporting) return;
    const QSignalBlocker blocker(m_imageList);
    int firstVisible = -1;
    for (int index = 0; index < m_items.size(); ++index) {
        const bool visible = matchesFilter(index);
        m_imageList->item(index)->setHidden(!visible);
        if (visible && firstVisible < 0) firstVisible = index;
    }
    if (!matchesFilter(m_currentIndex)) {
        if (firstVisible >= 0) setCurrentIndex(firstVisible);
        else {
            m_currentIndex = -1;
            m_imageAvailable = false;
            m_imageList->setCurrentRow(-1);
            m_imageView->setImage(QImage());
        }
    }
    refreshNavigation();
}

int CropReviewWorkspace::adjacentVisibleIndex(int direction) const
{
    if (m_currentIndex < 0) return -1;
    for (int index = m_currentIndex + direction; index >= 0 && index < m_items.size(); index += direction) {
        if (matchesFilter(index)) return index;
    }
    return -1;
}

CropReviewSession CropReviewWorkspace::currentSession() const
{
    CropReviewSession session;
    session.items = m_items;
    session.modelPath = m_modelPath;
    session.cropMarginPixels = m_cropMarginPixels;
    session.predictionThreshold = m_predictionThreshold;
    session.currentIndex = std::max(0, m_currentIndex);
    return session;
}

bool CropReviewWorkspace::writeSession()
{
    QString path = m_sessionPath;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, "Save Crop Review Session", "crop-review.wormreview.json",
                                            "WormProfiler Review (*.wormreview.json);;JSON (*.json)");
        if (path.isEmpty()) return false;
        if (!path.endsWith(".json", Qt::CaseInsensitive)) path += ".wormreview.json";
    }
    QString error;
    if (!currentSession().save(path, &error)) {
        QMessageBox::warning(this, "Session Not Saved", error);
        return false;
    }
    m_sessionPath = path;
    m_dirty = false;
    refreshNavigation();
    return true;
}

void CropReviewWorkspace::saveSession()
{
    if (!m_exporting && !m_externalBusy && hasSession()) writeSession();
}

void CropReviewWorkspace::saveSessionAs()
{
    if (m_exporting || m_externalBusy || !hasSession()) return;
    const QString previousPath = m_sessionPath;
    m_sessionPath.clear();
    if (!writeSession()) m_sessionPath = previousPath;
}

bool CropReviewWorkspace::maybeSave()
{
    if (m_exporting) return false;
    if (!m_dirty) return true;
    const auto answer = QMessageBox::question(this, "Save Crop Review?", "Save your crop adjustments and review decisions?",
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                              QMessageBox::Save);
    return answer == QMessageBox::Discard || (answer == QMessageBox::Save && writeSession());
}

void CropReviewWorkspace::closeEvent(QCloseEvent *event)
{
    if (maybeSave()) event->accept();
    else event->ignore();
}

QRect CropReviewWorkspace::clampedRectForCurrentImage(const QRect &cropRect) const
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

QImage CropReviewWorkspace::matToDisplayImage(const cv::Mat &mat) const
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
