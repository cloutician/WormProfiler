#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "CropReviewWorkspace.h"
#include "ImageLoader.h"
#include "ImageViewer.h"
#include "DesktopIcons.h"
#include "WormProfileWorkspace.h"
#include "PreprocessingPipeline.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPromise>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <vector>

namespace {
QLabel *section(QVBoxLayout *layout, const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName("sectionTitle");
    layout->addWidget(label);
    return label;
}

QString imageDescription(const cv::Mat &image)
{
    if (image.empty()) return QString();
    const int bits = static_cast<int>(image.elemSize1()) * 8;
    return QString("%1 x %2 px  |  %3-bit  |  %4 channel%5")
        .arg(image.cols).arg(image.rows).arg(bits).arg(image.channels()).arg(image.channels() == 1 ? "" : "s");
}

QWidget *pathField(QLineEdit *edit, QWidget *parent, const std::function<void()> &browse)
{
    auto *field = new QWidget(parent);
    auto *layout = new QHBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(edit, 1);
    auto *button = new QToolButton(field);
    button->setIcon(parent->style()->standardIcon(QStyle::SP_DirOpenIcon));
    button->setToolTip("Browse...");
    QObject::connect(button, &QToolButton::clicked, field, browse);
    layout->addWidget(button);
    return field;
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowIcon(DesktopIcons::icon(DesktopIcons::Symbol::Application));
    buildPreparationWorkspace();
    m_review = new CropReviewWorkspace({}, this);
    m_profiles = new WormProfileWorkspace(this);
    ui->workspaces->addTab(m_preparation, DesktopIcons::icon(DesktopIcons::Symbol::Adjustments), "Image preparation");
    ui->workspaces->addTab(m_review, DesktopIcons::icon(DesktopIcons::Symbol::Crop), "Crop review");
    ui->workspaces->addTab(m_profiles, DesktopIcons::icon(DesktopIcons::Symbol::Measurements), "Worm profiles");
    buildMenus();
    buildStatusBar();
    connect(ui->workspaces, &QTabWidget::currentChanged, this, &MainWindow::updateWorkspace);
    connect(m_review, &CropReviewWorkspace::newReviewRequested, this, &MainWindow::applyModelCropFolder);
    connect(m_review, &CropReviewWorkspace::openSessionRequested, this, &MainWindow::openCropReviewSession);
    connect(m_review, &CropReviewWorkspace::openSourceRequested, this, [this](const QString &path) {
        if (loadImagePath(path)) ui->workspaces->setCurrentIndex(0);
    });
    connect(m_review, &CropReviewWorkspace::stateChanged, this, &MainWindow::updateActions);
    connect(m_profiles, &WormProfileWorkspace::stateChanged, this, &MainWindow::updateActions);
    for (auto *action : {m_profiles->newAction(), m_profiles->openAction()}) {
        connect(action, &QAction::triggered, this, [this] { ui->workspaces->setCurrentIndex(2); });
    }
    connect(m_profiles, &WormProfileWorkspace::statusMessage, this, [this](const QString &message) {
        if (m_statusLabel) { m_statusLabel->setText(message); m_statusLabel->setToolTip(message); }
    });
    connect(m_profiles, &WormProfileWorkspace::jobProgress, this, [this](int value, int maximum) {
        m_progress->setRange(0, maximum); m_progress->setValue(value);
    });
    connect(m_profiles, &WormProfileWorkspace::busyChanged, this, [this](bool busy) {
        setJobBusy(busy, busy ? "Measuring segmentation TIFFs..." : QString());
        if (!busy && m_closeWhenIdle) QTimer::singleShot(0, this, &QWidget::close);
    });
    connect(m_profiles->findChild<ImageViewer *>(), &ImageViewer::zoomChanged, this, [this](double factor) {
        if (m_zoomLabel && ui->workspaces->currentIndex() == 2) m_zoomLabel->setText(QString("Zoom %1 %").arg(qRound(factor * 100)));
    });
    connect(m_review->findChild<CropImageView *>(), &CropImageView::zoomChanged, this, [this](double factor) {
        if (ui->workspaces->currentIndex() == 1) m_zoomLabel->setText(QString("Zoom %1 %").arg(qRound(factor * 100)));
    });
    connect(m_review, &CropReviewWorkspace::statusMessage, this, [this](const QString &message) {
        m_statusLabel->setText(message);
        m_statusLabel->setToolTip(message);
    });
    connect(m_review, &CropReviewWorkspace::jobProgress, this, [this](int value, int maximum) {
        m_progress->setRange(0, maximum); m_progress->setValue(value);
    });
    connect(m_review, &CropReviewWorkspace::busyChanged, this, [this](bool busy) {
        setJobBusy(busy, busy ? "Exporting approved crops..." : QString());
        if (!busy && m_closeWhenIdle) QTimer::singleShot(0, this, &QWidget::close);
    });
    applyProfile(PreprocessingProfile());
    const auto geometry = QSettings().value("window/geometry").toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    m_preparationSplitter->restoreState(QSettings().value("window/preparationPanes").toByteArray());
    m_review->findChild<QSplitter *>("reviewSplitter")->restoreState(QSettings().value("window/reviewPanes").toByteArray());
    m_profiles->findChild<QSplitter *>("wormProfileSplitter")->restoreState(QSettings().value("window/wormProfilePanes").toByteArray());
    if (QScreen *display = screen()) {
        const QRect available = display->availableGeometry();
        resize(size().boundedTo((available.size() - QSize(40, 60)).expandedTo(minimumSize())));
        if (!available.contains(frameGeometry())) move(available.topLeft() + QPoint(20, 20));
    }
    updateWorkspace();
}

MainWindow::~MainWindow()
{
    if (m_jobCancel) m_jobCancel->store(true);
    if (m_jobWatcher) m_jobWatcher->waitForFinished();
    delete ui;
}

void MainWindow::prepareForDisplay()
{
    if (isVisible()) return;
    ensurePolished();
    winId();
    // Resolve initial layouts, font glyphs, and icon rasters before splash animation.
    QPixmap firstFrame(size() * devicePixelRatioF());
    firstFrame.setDevicePixelRatio(devicePixelRatioF());
    render(&firstFrame);
}

void MainWindow::buildPreparationWorkspace()
{
    m_preparation = new QWidget(this);
    m_preparation->setObjectName("preparationWorkspace");
    auto *root = new QVBoxLayout(m_preparation);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    m_preparationToolbar = new QToolBar(m_preparation);
    m_preparationToolbar->setIconSize(QSize(18, 18));
    m_preparationToolbar->setObjectName("preparationToolbar");
    root->addWidget(m_preparationToolbar);
    m_preparationSplitter = new QSplitter(Qt::Horizontal, m_preparation);
    m_preparationSplitter->setObjectName("preparationSplitter");
    m_preparationSplitter->setChildrenCollapsible(false);
    auto *browser = new QWidget(m_preparationSplitter);
    browser->setObjectName("imageBrowser");
    browser->setMinimumWidth(170);
    auto *browserLayout = new QVBoxLayout(browser);
    browserLayout->setContentsMargins(10, 8, 10, 8);
    auto *title = new QLabel("Images", browser);
    title->setObjectName("panelTitle");
    browserLayout->addWidget(title);
    m_imageSearch = new QLineEdit(browser);
    m_imageSearch->setObjectName("imageSearch");
    m_imageSearch->setPlaceholderText("Filter images");
    m_imageSearch->setClearButtonEnabled(true);
    auto *searchAction = m_imageSearch->addAction(DesktopIcons::icon(DesktopIcons::Symbol::Search), QLineEdit::LeadingPosition);
    searchAction->setObjectName("imageSearchAction");
    browserLayout->addWidget(m_imageSearch);
    m_imageList = new QListWidget(browser);
    m_imageList->setObjectName("preparationImageList");
    m_imageList->setIconSize(QSize(64, 48));
    m_imageList->setTextElideMode(Qt::ElideMiddle);
    browserLayout->addWidget(m_imageList, 1);
    m_folderLabel = new QLabel("0 images", browser);
    m_folderLabel->setObjectName("secondaryText");
    m_folderLabel->setWordWrap(true);
    browserLayout->addWidget(m_folderLabel);

    auto *document = new QWidget(m_preparationSplitter);
    auto *documentLayout = new QVBoxLayout(document);
    documentLayout->setContentsMargins(0, 0, 0, 0);
    documentLayout->setSpacing(0);
    m_documentLabel = new QLabel("Image preparation", document);
    m_documentLabel->setObjectName("imageHeader");
    m_documentLabel->setTextFormat(Qt::PlainText);
    m_documentLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    documentLayout->addWidget(m_documentLabel);
    auto *comparison = new QSplitter(Qt::Horizontal, document);
    comparison->setObjectName("comparisonSplitter");
    comparison->setChildrenCollapsible(false);
    const auto addView = [comparison](const QString &name, ImageViewer **view, QLabel **metadata, QLabel **heading = nullptr) {
        auto *pane = new QWidget(comparison);
        auto *layout = new QVBoxLayout(pane);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto *label = new QLabel(name, pane);
        label->setObjectName("imageHeader");
        layout->addWidget(label);
        if (heading) *heading = label;
        *view = new ImageViewer(pane);
        layout->addWidget(*view, 1);
        *metadata = new QLabel("No image", pane);
        (*metadata)->setObjectName("imageMetadata");
        (*metadata)->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(*metadata);
        comparison->addWidget(pane);
    };
    addView("Original", &m_originalView, &m_originalInfo);
    addView("Processed", &m_processedView, &m_processedInfo, &m_processedHeading);
    m_originalView->setObjectName("originalImageView");
    m_processedView->setObjectName("processedImageView");
    comparison->setSizes({400, 400});
    documentLayout->addWidget(comparison, 1);

    auto *scroll = new QScrollArea(m_preparationSplitter);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(250);
    m_inspector = new QWidget(scroll);
    m_inspector->setObjectName("inspector");
    auto *properties = new QVBoxLayout(m_inspector);
    properties->setContentsMargins(14, 8, 14, 12);
    properties->setSpacing(8);
    auto *propertiesTitle = new QLabel("Image properties", m_inspector);
    propertiesTitle->setObjectName("panelTitle");
    properties->addWidget(propertiesTitle);
    section(properties, "Intensity");
    m_normalize = new QCheckBox("Normalize intensity", m_inspector);
    m_normalize->setObjectName("normalizeCheckBox");
    m_grayscale = new QCheckBox("Grayscale", m_inspector);
    m_grayscale->setObjectName("grayscaleCheckBox");
    m_clahe = new QCheckBox("Local contrast (CLAHE)", m_inspector);
    m_clahe->setObjectName("claheCheckBox");
    for (auto *check : {m_normalize, m_grayscale, m_clahe}) properties->addWidget(check);
    section(properties, "Smoothing");
    auto *blurRow = new QHBoxLayout();
    blurRow->addWidget(new QLabel("Gaussian blur", m_inspector), 1);
    m_blurValue = new QSpinBox(m_inspector);
    m_blurValue->setRange(0, 10);
    m_blurValue->setFixedWidth(76);
    blurRow->addWidget(m_blurValue);
    properties->addLayout(blurRow);
    m_blur = new QSlider(Qt::Horizontal, m_inspector);
    m_blur->setObjectName("blurSlider");
    m_blur->setAccessibleName("Gaussian blur");
    m_blur->setRange(0, 10);
    properties->addWidget(m_blur);
    section(properties, "Geometry");
    m_resize = new QCheckBox("Resize image", m_inspector);
    m_resize->setObjectName("resizeCheckBox");
    auto *resizeRow = new QHBoxLayout();
    resizeRow->addWidget(m_resize, 1);
    m_scaleValue = new QSpinBox(m_inspector);
    m_scaleValue->setRange(10, 100);
    m_scaleValue->setSuffix(" %");
    m_scaleValue->setFixedWidth(76);
    resizeRow->addWidget(m_scaleValue);
    properties->addLayout(resizeRow);
    m_scale = new QSlider(Qt::Horizontal, m_inspector);
    m_scale->setObjectName("resizeSlider");
    m_scale->setAccessibleName("Resize percentage");
    m_scale->setRange(10, 100);
    properties->addWidget(m_scale);
    m_autoCrop = new QCheckBox("Automatic trim", m_inspector);
    m_autoCrop->setObjectName("autoCropCheckBox");
    m_autoCrop->setToolTip("Trim the image using classical worm-boundary detection");
    properties->addWidget(m_autoCrop);
    auto *marginRow = new QHBoxLayout();
    marginRow->addWidget(new QLabel("Trim margin", m_inspector), 1);
    m_margin = new QSpinBox(m_inspector);
    m_margin->setObjectName("cropMarginSpinBox");
    m_margin->setRange(0, 250);
    m_margin->setSuffix(" px");
    m_margin->setFixedWidth(90);
    marginRow->addWidget(m_margin);
    properties->addLayout(marginRow);
    section(properties, "Mask overlay");
    m_loadMaskButton = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogContentsView), "Load mask...", m_inspector);
    m_loadMaskButton->setObjectName("loadMaskButton");
    properties->addWidget(m_loadMaskButton);
    m_maskLabel = new QLabel("No mask loaded", m_inspector);
    m_maskLabel->setObjectName("secondaryText");
    m_maskLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    properties->addWidget(m_maskLabel);
    m_showMask = new QCheckBox("Show overlay", m_inspector);
    m_showMask->setObjectName("showMaskCheckBox");
    auto *opacityRow = new QHBoxLayout();
    opacityRow->addWidget(m_showMask, 1);
    m_maskOpacityValue = new QSpinBox(m_inspector);
    m_maskOpacityValue->setRange(0, 100);
    m_maskOpacityValue->setValue(45);
    m_maskOpacityValue->setSuffix(" %");
    m_maskOpacityValue->setFixedWidth(76);
    opacityRow->addWidget(m_maskOpacityValue);
    properties->addLayout(opacityRow);
    m_maskOpacity = new QSlider(Qt::Horizontal, m_inspector);
    m_maskOpacity->setRange(0, 100);
    m_maskOpacity->setValue(45);
    m_maskOpacity->setToolTip("Mask opacity");
    m_maskOpacity->setAccessibleName("Mask opacity");
    properties->addWidget(m_maskOpacity);
    properties->addStretch();
    scroll->setWidget(m_inspector);
    m_preparationSplitter->addWidget(browser);
    m_preparationSplitter->addWidget(document);
    m_preparationSplitter->addWidget(scroll);
    m_preparationSplitter->setStretchFactor(0, 0);
    m_preparationSplitter->setStretchFactor(1, 1);
    m_preparationSplitter->setStretchFactor(2, 0);
    m_preparationSplitter->setSizes({220, 900, 272});
    root->addWidget(m_preparationSplitter, 1);

    connect(m_imageList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < m_imagePaths.size() && !loadImagePath(m_imagePaths[row])) {
            m_imageList->item(row)->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
            const QSignalBlocker blocker(m_imageList);
            m_imageList->setCurrentRow(m_imagePaths.indexOf(m_currentPath));
        }
    });
    connect(m_imageList, &QListWidget::itemDoubleClicked, this, [this] { if (!m_currentPath.isEmpty()) loadImagePath(m_currentPath); });
    connect(m_imageSearch, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int index = 0; index < m_imagePaths.size(); ++index) {
            m_imageList->item(index)->setHidden(!QFileInfo(m_imagePaths[index]).fileName().contains(text, Qt::CaseInsensitive));
        }
        updateActions();
    });
    connect(m_blur, &QSlider::valueChanged, m_blurValue, &QSpinBox::setValue);
    connect(m_blurValue, &QSpinBox::valueChanged, m_blur, &QSlider::setValue);
    connect(m_scale, &QSlider::valueChanged, m_scaleValue, &QSpinBox::setValue);
    connect(m_scaleValue, &QSpinBox::valueChanged, m_scale, &QSlider::setValue);
    for (auto *check : {m_normalize, m_grayscale, m_clahe, m_resize, m_autoCrop}) {
        connect(check, &QCheckBox::toggled, this, &MainWindow::updateProcessedImage);
    }
    connect(m_blur, &QSlider::valueChanged, this, &MainWindow::updateProcessedImage);
    connect(m_scale, &QSlider::valueChanged, this, &MainWindow::updateProcessedImage);
    connect(m_margin, &QSpinBox::valueChanged, this, &MainWindow::updateProcessedImage);
    connect(m_loadMaskButton, &QPushButton::clicked, this, &MainWindow::loadMask);
    connect(m_maskOpacityValue, &QSpinBox::valueChanged, m_maskOpacity, &QSlider::setValue);
    connect(m_maskOpacity, &QSlider::valueChanged, m_maskOpacityValue, &QSpinBox::setValue);
    connect(m_showMask, &QCheckBox::toggled, this, &MainWindow::refreshImagePreviews);
    connect(m_maskOpacity, &QSlider::valueChanged, this, &MainWindow::updateOverlay);
    connect(m_processedView, &ImageViewer::zoomChanged, this, [this](double factor) {
        if (m_zoomLabel) m_zoomLabel->setText(QString("Processed %1 %").arg(qRound(factor * 100)));
    });
    connect(m_originalView, &ImageViewer::zoomChanged, this, [this](double factor) {
        if (m_zoomLabel) m_zoomLabel->setText(QString("Original %1 %").arg(qRound(factor * 100)));
    });
}

void MainWindow::buildMenus()
{
    auto *fileMenu = ui->menubar->addMenu("&File");
    auto *open = fileMenu->addAction(style()->standardIcon(QStyle::SP_FileIcon), "Open image...", this, &MainWindow::openImage);
    open->setShortcut(QKeySequence::Open);
    auto *folder = fileMenu->addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), "Open folder...", this, &MainWindow::openFolder);
    folder->setShortcut(QKeySequence("Ctrl+Shift+O"));
    fileMenu->addAction(m_review->openAction());
    fileMenu->addAction(m_profiles->openAction());
    fileMenu->addSeparator();
    auto *save = fileMenu->addAction("Save...", this, [this] {
        if (m_busy && ui->workspaces->currentIndex() != 0) return;
        if (ui->workspaces->currentIndex() == 2) m_profiles->saveProfile();
        else if (ui->workspaces->currentIndex() == 1) m_review->saveSession();
        else saveProcessedImage();
    });
    m_saveDocumentAction = save;
    save->setIcon(DesktopIcons::icon(DesktopIcons::Symbol::Save));
    save->setShortcut(QKeySequence::Save);
    m_exportImageAction = fileMenu->addAction(DesktopIcons::icon(DesktopIcons::Symbol::Export), "Export processed image...", this, &MainWindow::saveProcessedImage);
    fileMenu->addAction(m_review->exportAction());
    fileMenu->addAction(m_profiles->exportAction());
    fileMenu->addSeparator();
    auto *quit = fileMenu->addAction("Exit", this, &QWidget::close);
    quit->setIcon(DesktopIcons::icon(DesktopIcons::Symbol::Exit));
    quit->setShortcut(QKeySequence::Quit);

    auto *processing = ui->menubar->addMenu("&Processing");
    auto *loadProfile = processing->addAction("Load profile...", this, &MainWindow::loadPreprocessingProfile);
    loadProfile->setIcon(DesktopIcons::icon(DesktopIcons::Symbol::FolderOpen));
    loadProfile->setObjectName("actionLoadPreprocessingProfile");
    auto *saveProfile = processing->addAction("Save profile...", this, &MainWindow::savePreprocessingProfile);
    saveProfile->setIcon(DesktopIcons::icon(DesktopIcons::Symbol::Save));
    auto *reset = processing->addAction(style()->standardIcon(QStyle::SP_BrowserReload), "Reset adjustments", this, [this] { applyProfile(PreprocessingProfile()); });
    processing->addSeparator();
    m_loadMaskAction = processing->addAction("Load mask...", this, &MainWindow::loadMask);
    m_loadMaskAction->setIcon(DesktopIcons::icon(DesktopIcons::Symbol::Layers));
    m_batchAction = processing->addAction(DesktopIcons::icon(DesktopIcons::Symbol::Batch), "Batch export...", this, &MainWindow::batchPreprocessFolder);
    m_batchAction->setObjectName("actionBatchExport");
    auto *review = ui->menubar->addMenu("&Review");
    review->addAction(m_review->newAction());
    review->addAction(m_review->openAction());
    review->addSeparator();
    review->addAction(m_review->saveAction());
    review->addAction(m_review->saveAsAction());
    review->addAction(m_review->exportAction());
    auto *profiles = ui->menubar->addMenu("&Worm profiles");
    profiles->addAction(m_profiles->newAction());
    profiles->addAction(m_profiles->openAction());
    profiles->addAction(m_profiles->saveAction());
    profiles->addAction("Save worm profile as...", m_profiles, &WormProfileWorkspace::saveProfileAs);
    profiles->addSeparator();
    profiles->addAction(m_profiles->exportAction());
    auto *view = ui->menubar->addMenu("&View");
    auto *browserAction = view->addAction("Image browser");
    browserAction->setCheckable(true);
    browserAction->setChecked(true);
    auto *inspectorAction = view->addAction("Properties inspector");
    inspectorAction->setCheckable(true);
    inspectorAction->setChecked(true);
    const auto currentSplitter = [this] {
        if (ui->workspaces->currentIndex() == 0) return m_preparationSplitter;
        if (ui->workspaces->currentIndex() == 1) return m_review->findChild<QSplitter *>("reviewSplitter");
        return m_profiles->findChild<QSplitter *>("wormProfileSplitter");
    };
    const auto togglePane = [currentSplitter](int pane, bool visible) {
        currentSplitter()->widget(pane)->setVisible(visible);
    };
    connect(browserAction, &QAction::toggled, this, [togglePane](bool value) { togglePane(0, value); });
    connect(inspectorAction, &QAction::toggled, this, [togglePane](bool value) { togglePane(2, value); });
    connect(view, &QMenu::aboutToShow, this, [currentSplitter, browserAction, inspectorAction] {
        auto *splitter = currentSplitter();
        const QSignalBlocker first(browserAction), second(inspectorAction);
        browserAction->setChecked(!splitter->widget(0)->isHidden());
        inspectorAction->setChecked(!splitter->widget(2)->isHidden());
    });
    view->addSeparator();
    m_fitAction = view->addAction(style()->standardIcon(QStyle::SP_TitleBarMaxButton), "Fit images", this, [this] {
        if (ui->workspaces->currentIndex() == 2) m_profiles->findChild<ImageViewer *>()->fitImage();
        else if (ui->workspaces->currentIndex() == 1) m_review->findChild<CropImageView *>()->fitImage();
        else { m_originalView->fitImage(); m_processedView->fitImage(); }
    });
    m_fitAction->setShortcut(QKeySequence("Ctrl+0"));
    m_actualSizeAction = view->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), "Actual size", this, [this] {
        if (ui->workspaces->currentIndex() == 2) m_profiles->findChild<ImageViewer *>()->actualSize();
        else if (ui->workspaces->currentIndex() == 1) m_review->findChild<CropImageView *>()->actualSize();
        else { m_originalView->actualSize(); m_processedView->actualSize(); }
    });
    m_actualSizeAction->setShortcut(QKeySequence("Ctrl+1"));
    auto *help = ui->menubar->addMenu("&Help");
    help->addAction("About WormProfiler", this, [this] {
        QMessageBox::about(this, "WormProfiler", "WormProfiler\nMicroscopy image preparation, crop review, and tissue morphometrics\nQt 6 and OpenCV");
    });

    m_preparationToolbar->addAction(open);
    m_preparationToolbar->addAction(folder);
    m_preparationToolbar->addAction(m_exportImageAction);
    m_preparationToolbar->addSeparator();
    m_previousAction = m_preparationToolbar->addAction(style()->standardIcon(QStyle::SP_ArrowBack), "Previous image", this, [this] { navigateImage(-1); });
    m_nextAction = m_preparationToolbar->addAction(style()->standardIcon(QStyle::SP_ArrowForward), "Next image", this, [this] { navigateImage(1); });
    m_previousAction->setShortcut(QKeySequence("Alt+Left"));
    m_nextAction->setShortcut(QKeySequence("Alt+Right"));
    m_preparationToolbar->addSeparator();
    m_preparationToolbar->addAction(m_fitAction);
    m_preparationToolbar->addAction(m_actualSizeAction);
    m_preparationToolbar->addSeparator();
    auto *profileButton = new QToolButton(m_preparationToolbar);
    profileButton->setObjectName("profilesMenuButton");
    profileButton->setText("Profiles");
    profileButton->setIcon(DesktopIcons::icon(DesktopIcons::Symbol::Adjustments));
    profileButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    profileButton->setPopupMode(QToolButton::InstantPopup);
    profileButton->setToolTip("Preprocessing profiles");
    auto *profileMenu = new QMenu(profileButton);
    profileMenu->addActions({loadProfile, saveProfile, reset});
    profileButton->setMenu(profileMenu);
    m_preparationToolbar->addWidget(profileButton);
    auto *spacer = new QWidget(m_preparationToolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_preparationToolbar->addWidget(spacer);
    m_preparationToolbar->addAction(m_batchAction);
    m_actualSizeAction->setIconText("1:1");
    for (QAction *action : {folder, m_batchAction, m_actualSizeAction}) {
        if (auto *button = qobject_cast<QToolButton *>(m_preparationToolbar->widgetForAction(action))) {
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        }
    }
    open->setToolTip("Open image (Ctrl+O)");
    m_fitAction->setToolTip("Fit both images to their views (Ctrl+0)");
    m_actualSizeAction->setToolTip("One image pixel per display pixel (Ctrl+1)");
}

void MainWindow::buildStatusBar()
{
    m_statusLabel = new QLabel("Ready", this);
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    ui->statusbar->addWidget(m_statusLabel, 1);
    m_progress = new QProgressBar(this);
    m_progress->setFixedWidth(160);
    m_progress->setTextVisible(false);
    m_progress->hide();
    ui->statusbar->addPermanentWidget(m_progress);
    m_cancelButton = new QToolButton(this);
    m_cancelButton->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));
    m_cancelButton->setToolTip("Cancel the running job");
    m_cancelButton->hide();
    ui->statusbar->addPermanentWidget(m_cancelButton);
    connect(m_cancelButton, &QToolButton::clicked, this, &MainWindow::cancelJob);
    m_zoomLabel = new QLabel(this);
    m_zoomLabel->setFixedWidth(125);
    m_zoomLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ui->statusbar->addPermanentWidget(m_zoomLabel);
}

void MainWindow::updateWorkspace()
{
    updateActions();
    if (m_zoomLabel) {
        const bool preparation = ui->workspaces->currentIndex() == 0;
        const double zoom = preparation ? m_processedView->zoomFactor() : ui->workspaces->currentIndex() == 1
            ? m_review->findChild<CropImageView *>()->zoomFactor() : m_profiles->findChild<ImageViewer *>()->zoomFactor();
        m_zoomLabel->setText(QString("%1 %2 %").arg(preparation ? "Processed" : "Zoom").arg(qRound(zoom * 100)));
    }
    if (!m_busy && m_statusLabel) {
        m_statusLabel->setText(ui->workspaces->currentIndex() == 0 ? (m_currentPath.isEmpty() ? "Ready" : m_currentPath)
            : ui->workspaces->currentIndex() == 1 ? (m_review->sessionPath().isEmpty() ? "Unsaved crop review" : m_review->sessionPath())
            : (m_profiles->profilePath().isEmpty() ? "Worm profiles" : m_profiles->profilePath()));
    }
}

void MainWindow::updateActions()
{
    if (!m_exportImageAction) return;
    const bool preparation = ui->workspaces->currentIndex() == 0;
    const bool hasImage = !m_currentOriginalImage.empty();
    m_exportImageAction->setEnabled(preparation && !m_currentProcessedImage.empty());
    m_saveDocumentAction->setEnabled(preparation ? !m_currentProcessedImage.empty()
        : (ui->workspaces->currentIndex() == 1 ? m_review->hasSession() : m_profiles->hasProfile()) && !m_busy);
    m_loadMaskAction->setEnabled(preparation && hasImage);
    m_loadMaskButton->setEnabled(hasImage);
    m_batchAction->setEnabled(!m_busy);
    m_fitAction->setEnabled(preparation ? hasImage : ui->workspaces->currentIndex() == 1 ? m_review->hasSession() && !m_busy
        : m_profiles->findChild<ImageViewer *>()->hasImage() && !m_busy);
    m_actualSizeAction->setEnabled(m_fitAction->isEnabled());
    const int row = m_imageList->currentRow();
    bool previous = false, next = false;
    for (int index = 0; index < m_imageList->count(); ++index) {
        if (!m_imageList->item(index)->isHidden()) { previous |= index < row; next |= index > row; }
    }
    m_previousAction->setEnabled(preparation && previous);
    m_nextAction->setEnabled(preparation && next);
    m_scale->setEnabled(m_resize->isChecked());
    m_scaleValue->setEnabled(m_resize->isChecked());
    m_margin->setEnabled(m_autoCrop->isChecked());
    m_showMask->setEnabled(!m_currentMask.empty());
    m_maskOpacity->setEnabled(!m_currentMask.empty() && m_showMask->isChecked());
    m_maskOpacityValue->setEnabled(m_maskOpacity->isEnabled());
    if (preparation && !hasImage && m_zoomLabel) m_zoomLabel->clear();
    m_review->newAction()->setEnabled(!m_busy);
    m_review->openAction()->setEnabled(!m_busy);
    m_review->setExternalBusy(m_jobWatcher != nullptr || m_profiles->isBusy());
    m_profiles->setExternalBusy(m_jobWatcher != nullptr || m_review->isBusy());
    ui->workspaces->setTabText(1, m_review->isDirty() ? "Crop review *" : "Crop review");
    ui->workspaces->setTabText(2, m_profiles->isDirty() ? "Worm profiles *" : "Worm profiles");
    const QString reviewName = m_review->sessionPath().isEmpty() ? "Untitled crop review"
                                                                 : QFileInfo(m_review->sessionPath()).fileName();
    setWindowTitle(preparation
        ? (m_currentPath.isEmpty() ? "WormProfiler" : QFileInfo(m_currentPath).fileName() + " - WormProfiler")
        : ui->workspaces->currentIndex() == 1 ? QString("%1%2 - WormProfiler").arg(m_review->isDirty() ? "* " : "").arg(reviewName)
        : QString("%1%2 - WormProfiler").arg(m_profiles->isDirty() ? "* " : "")
            .arg(m_profiles->hasProfile() ? m_profiles->profile().wormId : "Worm profiles"));
}

void MainWindow::setImageFiles(const QStringList &paths, const QString &folder)
{
    ++m_thumbnailGeneration;
    m_imagePaths = paths;
    m_currentFolder = folder;
    {
        const QSignalBlocker blocker(m_imageList), searchBlocker(m_imageSearch);
        m_imageList->clear();
        m_imageSearch->clear();
        for (const QString &path : paths) {
            auto *item = new QListWidgetItem(style()->standardIcon(QStyle::SP_FileIcon), QFileInfo(path).fileName(), m_imageList);
            item->setToolTip(path);
            item->setSizeHint(QSize(180, 62));
        }
        int row = paths.indexOf(m_currentPath);
        if (row < 0 && !paths.isEmpty()) row = 0;
        m_imageList->setCurrentRow(row);
    }
    m_folderLabel->setText(QString("%1 images%2").arg(paths.size())
        .arg(folder.isEmpty() ? QString() : "\n" + QDir(folder).dirName()));
    m_folderLabel->setToolTip(folder);
    if (!paths.contains(m_currentPath)) {
        m_currentPath.clear();
        m_currentOriginalImage.release(); m_currentProcessedImage.release(); m_currentMask.release();
        m_currentOverlayImage.release(); m_showMask->setChecked(false);
        m_documentLabel->setText("Image preparation");
        m_maskLabel->setText("No mask loaded");
        bool loaded = false;
        for (int index = 0; index < paths.size(); ++index) {
            if (loadImagePath(paths[index])) { loaded = true; break; }
            m_imageList->item(index)->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
        }
        if (!loaded) {
            const QSignalBlocker blocker(m_imageList);
            m_imageList->setCurrentRow(-1);
        }
        refreshImagePreviews();
    }
    const int generation = m_thumbnailGeneration;
    QTimer::singleShot(0, this, [this, generation] { loadThumbnail(0, generation); });
    updateActions();
}

void MainWindow::loadThumbnail(int index, int generation)
{
    if (generation != m_thumbnailGeneration || index >= m_imagePaths.size()) return;
    QImageReader reader(m_imagePaths[index]);
    const QSize size = reader.size();
    if (size.isValid()) reader.setScaledSize(size.scaled(QSize(64, 48), Qt::KeepAspectRatio));
    const QImage thumbnail = reader.read();
    if (!thumbnail.isNull()) m_imageList->item(index)->setIcon(QPixmap::fromImage(thumbnail));
    QTimer::singleShot(0, this, [this, index, generation] { loadThumbnail(index + 1, generation); });
}

bool MainWindow::loadImagePath(const QString &path)
{
    cv::Mat loaded;
    try { loaded = ImageLoader().loadWithOpenCV(path); } catch (const cv::Exception &) { }
    if (loaded.empty() || ImageLoader().matToQImage(loaded).isNull()) {
        m_statusLabel->setText("Unable to open image: " + path);
        return false;
    }
    m_currentOriginalImage = loaded;
    m_currentPath = QFileInfo(path).absoluteFilePath();
    m_currentMask.release(); m_currentOverlayImage.release();
    m_maskLabel->setText("No mask loaded");
    m_showMask->setChecked(false);
    if (!m_imagePaths.contains(m_currentPath)) {
        auto paths = m_imagePaths; paths.append(m_currentPath);
        setImageFiles(paths, m_currentFolder);
    }
    {
        const QSignalBlocker blocker(m_imageList);
        m_imageList->setCurrentRow(m_imagePaths.indexOf(m_currentPath));
    }
    m_documentLabel->setText(QFileInfo(path).fileName());
    m_documentLabel->setToolTip(path);
    m_originalView->fitImage(); m_processedView->fitImage();
    updateProcessedImage();
    m_statusLabel->setText(m_currentPath);
    QSettings().setValue("paths/images", QFileInfo(path).absolutePath());
    return true;
}

void MainWindow::openImage()
{
    const QString path = QFileDialog::getOpenFileName(this, "Open Image", QSettings().value("paths/images").toString(),
        "Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)");
    if (path.isEmpty()) return;
    if (loadImagePath(path)) ui->workspaces->setCurrentIndex(0);
    else QMessageBox::warning(this, "Image Not Opened", "The selected image could not be read.");
}

void MainWindow::openFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, "Open Image Folder", QSettings().value("paths/images").toString());
    if (folder.isEmpty()) return;
    const auto files = ProcessingJobs::imageFiles(folder);
    if (files.isEmpty()) { QMessageBox::information(this, "No Images", "No supported images were found in this folder."); return; }
    setImageFiles(files, folder);
    ui->workspaces->setCurrentIndex(0);
}

void MainWindow::navigateImage(int direction)
{
    for (int row = m_imageList->currentRow() + direction; row >= 0 && row < m_imageList->count(); row += direction) {
        if (!m_imageList->item(row)->isHidden()) { m_imageList->setCurrentRow(row); break; }
    }
}

PreprocessingProfile MainWindow::currentProfile() const
{
    PreprocessingProfile profile;
    profile.blurStrength = m_blur->value();
    profile.normalizeEnabled = m_normalize->isChecked();
    profile.resizeEnabled = m_resize->isChecked();
    profile.resizePercent = m_scale->value();
    profile.grayscaleEnabled = m_grayscale->isChecked();
    profile.claheEnabled = m_clahe->isChecked();
    profile.autoCropEnabled = m_autoCrop->isChecked();
    profile.cropMarginPixels = m_margin->value();
    return profile;
}

void MainWindow::applyProfile(const PreprocessingProfile &profile)
{
    {
        const QSignalBlocker a(m_blur), b(m_normalize), c(m_resize), d(m_scale), e(m_grayscale),
            f(m_clahe), g(m_autoCrop), h(m_margin);
        m_blur->setValue(profile.blurStrength); m_blurValue->setValue(profile.blurStrength);
        m_normalize->setChecked(profile.normalizeEnabled);
        m_resize->setChecked(profile.resizeEnabled);
        m_scale->setValue(profile.resizePercent); m_scaleValue->setValue(profile.resizePercent);
        m_grayscale->setChecked(profile.grayscaleEnabled);
        m_clahe->setChecked(profile.claheEnabled);
        m_autoCrop->setChecked(profile.autoCropEnabled);
        m_margin->setValue(profile.cropMarginPixels);
    }
    updateProcessedImage();
}

void MainWindow::updateProcessedImage()
{
    updateActions();
    if (m_currentOriginalImage.empty()) return;
    const auto profile = currentProfile();
    try {
        m_currentProcessedImage = PreprocessingPipeline().preprocess(m_currentOriginalImage, profile.blurStrength,
            profile.normalizeEnabled, profile.resizeEnabled, profile.resizePercent / 100.0,
            profile.grayscaleEnabled, profile.claheEnabled, profile.autoCropEnabled, profile.cropMarginPixels);
    } catch (const cv::Exception &exception) {
        m_currentProcessedImage.release();
        m_statusLabel->setText("Processing failed: " + QString::fromStdString(exception.what()));
    }
    refreshImagePreviews();
}

void MainWindow::refreshImagePreviews()
{
    ImageLoader loader;
    m_originalView->setImage(loader.matToQImage(m_currentOriginalImage));
    const bool overlay = m_showMask->isChecked() && !m_currentOverlayImage.empty();
    const cv::Mat &display = overlay ? m_currentOverlayImage : m_currentProcessedImage;
    m_processedView->setImage(loader.matToQImage(display));
    m_processedHeading->setText(overlay ? "Mask overlay" : "Processed");
    m_originalInfo->setText(imageDescription(m_currentOriginalImage));
    m_originalInfo->setToolTip(m_originalInfo->text());
    m_processedInfo->setText(imageDescription(display));
    m_processedInfo->setToolTip(m_processedInfo->text());
    updateActions();
}

void MainWindow::loadMask()
{
    if (m_currentOriginalImage.empty()) return;
    const QString path = QFileDialog::getOpenFileName(this, "Load Mask", QFileInfo(m_currentPath).absolutePath(),
                                                     "Masks (*.png *.tif *.tiff)");
    if (path.isEmpty()) return;
    cv::Mat mask;
    try { mask = ImageLoader().loadWithOpenCV(path); } catch (const cv::Exception &) { }
    if (mask.empty() || mask.size() != m_currentOriginalImage.size()
        || (mask.channels() != 1 && mask.channels() != 3 && mask.channels() != 4)) {
        QMessageBox::warning(this, "Mask Not Loaded", "The mask must be readable and match the original image dimensions.");
        return;
    }
    m_currentMask = mask;
    m_maskLabel->setText(QFileInfo(path).fileName());
    m_maskLabel->setToolTip(path);
    updateOverlay();
    m_showMask->setChecked(true);
}

void MainWindow::updateOverlay()
{
    if (m_currentOriginalImage.empty() || m_currentMask.empty()) return;
    const QImage source = ImageLoader().matToQImage(m_currentOriginalImage).convertToFormat(QImage::Format_RGB888);
    cv::Mat rgb(source.height(), source.width(), CV_8UC3, const_cast<uchar *>(source.constBits()), source.bytesPerLine());
    cv::Mat original;
    cv::cvtColor(rgb, original, cv::COLOR_RGB2BGR);
    cv::Mat gray;
    if (m_currentMask.channels() == 3) cv::cvtColor(m_currentMask, gray, cv::COLOR_BGR2GRAY);
    else if (m_currentMask.channels() == 4) cv::cvtColor(m_currentMask, gray, cv::COLOR_BGRA2GRAY);
    else gray = m_currentMask;
    cv::Mat foreground;
    cv::compare(gray, 0, foreground, cv::CMP_GT);
    const cv::Mat tint(original.size(), CV_8UC3, cv::Scalar(125, 190, 35));
    cv::Mat blended;
    const double alpha = m_maskOpacity->value() / 100.0;
    cv::addWeighted(original, 1.0 - alpha, tint, alpha, 0, blended);
    m_currentOverlayImage = original.clone();
    blended.copyTo(m_currentOverlayImage, foreground);
    refreshImagePreviews();
}

void MainWindow::saveProcessedImage()
{
    if (m_currentProcessedImage.empty()) return;
    QString path = QFileDialog::getSaveFileName(this, "Export Processed Image",
        QFileInfo(m_currentPath).absolutePath() + "/" + QFileInfo(m_currentPath).completeBaseName() + "_processed.png",
        "PNG (*.png);;TIFF (*.tif *.tiff);;JPEG (*.jpg *.jpeg)");
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += ".png";
    try {
        if (QFileInfo(path).suffix().compare("png", Qt::CaseInsensitive) == 0
            && m_currentProcessedImage.depth() != CV_8U && m_currentProcessedImage.depth() != CV_16U) {
            throw std::runtime_error("Choose TIFF to preserve this image's pixel type.");
        }
        std::vector<uchar> data;
        if (!cv::imencode(("." + QFileInfo(path).suffix()).toStdString(), m_currentProcessedImage, data)) {
            throw std::runtime_error("Image encoding failed.");
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)
            || file.write(reinterpret_cast<const char *>(data.data()), static_cast<qint64>(data.size())) != data.size()
            || !file.commit()) throw std::runtime_error(file.errorString().toStdString());
        m_statusLabel->setText("Exported: " + path);
    } catch (const std::exception &exception) {
        QMessageBox::warning(this, "Export Failed", QString::fromUtf8(exception.what()));
    }
}

void MainWindow::savePreprocessingProfile()
{
    QString path = QFileDialog::getSaveFileName(this, "Save Preprocessing Profile", "preprocessing-profile.json",
                                               "WormProfiler JSON Profile (*.json)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".json", Qt::CaseInsensitive)) path += ".json";
    QString error;
    if (!currentProfile().save(path, &error)) { QMessageBox::warning(this, "Profile Not Saved", error); return; }
    m_statusLabel->setText("Profile saved: " + QFileInfo(path).fileName());
}

void MainWindow::loadPreprocessingProfile()
{
    const QString path = QFileDialog::getOpenFileName(this, "Load Preprocessing Profile", QString(),
                                                     "WormProfiler JSON Profile (*.json);;All Files (*.*)");
    if (path.isEmpty()) return;
    PreprocessingProfile profile;
    QString error;
    if (!PreprocessingProfile::load(path, &profile, &error)) { QMessageBox::warning(this, "Profile Not Loaded", error); return; }
    applyProfile(profile);
    ui->workspaces->setCurrentIndex(0);
    m_statusLabel->setText("Profile loaded: " + QFileInfo(path).fileName());
}

void MainWindow::showReviewSession(const CropReviewSession &session, const QString &path)
{
    m_review->setSession(session, path);
    ui->workspaces->setCurrentIndex(1);
    updateActions();
}

void MainWindow::openCropReviewSession()
{
    if (m_busy || !m_review->maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, "Open Crop Review Session", QSettings().value("paths/reviews").toString(),
                                                     "WormProfiler Review (*.wormreview.json *.json)");
    if (path.isEmpty()) return;
    CropReviewSession session;
    QString error;
    if (!CropReviewSession::load(path, &session, &error)) { QMessageBox::warning(this, "Session Not Opened", error); return; }
    QSettings().setValue("paths/reviews", QFileInfo(path).absolutePath());
    showReviewSession(session, path);
}

void MainWindow::applyModelCropFolder()
{
    if (m_busy || !m_review->maybeSave()) return;
    QSettings settings;
    QDialog dialog(this);
    dialog.setObjectName("newReviewDialog");
    dialog.setWindowTitle("New Crop Review");
    dialog.setMinimumWidth(560);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *folder = new QLineEdit(m_currentFolder.isEmpty() ? settings.value("paths/images").toString() : m_currentFolder, &dialog);
    folder->setObjectName("reviewInputFolder");
    auto *model = new QLineEdit(settings.value("paths/model").toString(), &dialog);
    model->setObjectName("reviewModelPath");
    form->addRow("Image folder", pathField(folder, &dialog, [&dialog, folder] {
        const QString path = QFileDialog::getExistingDirectory(&dialog, "Image Folder", folder->text());
        if (!path.isEmpty()) folder->setText(path);
    }));
    form->addRow("Crop model", pathField(model, &dialog, [&dialog, model] {
        const QString path = QFileDialog::getOpenFileName(&dialog, "Crop Model", model->text(), "ONNX Model (*.onnx)");
        if (!path.isEmpty()) model->setText(path);
    }));
    auto *margin = new QSpinBox(&dialog);
    margin->setRange(0, 1000);
    margin->setValue(settings.value("review/margin", 40).toInt());
    margin->setSuffix(" px");
    margin->setObjectName("reviewMargin");
    auto *threshold = new QDoubleSpinBox(&dialog);
    threshold->setRange(0.01, 0.99);
    threshold->setDecimals(2);
    threshold->setSingleStep(0.05);
    threshold->setValue(settings.value("review/threshold", 0.65).toDouble());
    threshold->setObjectName("reviewThreshold");
    form->addRow("Crop margin", margin);
    form->addRow("Mask threshold", threshold);
    layout->addLayout(form);
    auto *error = new QLabel(&dialog);
    error->setWordWrap(true);
    error->setStyleSheet("color: #914d16;");
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Run model");
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    layout->addWidget(buttons);
    QStringList files;
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        files = ProcessingJobs::imageFiles(folder->text());
        if (files.isEmpty()) { error->setText("Choose a folder containing supported images."); return; }
        if (!QFileInfo(model->text()).isFile()) { error->setText("Choose an ONNX crop model."); return; }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return;
    settings.setValue("paths/images", folder->text());
    settings.setValue("paths/model", model->text());
    settings.setValue("review/margin", margin->value());
    settings.setValue("review/threshold", threshold->value());
    startJob(true, files, model->text(), margin->value(), threshold->value());
}

void MainWindow::batchPreprocessFolder()
{
    if (m_busy) return;
    QDialog dialog(this);
    dialog.setObjectName("batchExportDialog");
    dialog.setWindowTitle("Batch Export");
    dialog.setMinimumWidth(560);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();
    auto *folder = new QLineEdit(m_currentFolder.isEmpty() ? QSettings().value("paths/images").toString() : m_currentFolder, &dialog);
    folder->setObjectName("batchInputFolder");
    auto *destination = new QLineEdit(folder->text().isEmpty() ? QString() : QDir(folder->text()).filePath("processed"), &dialog);
    destination->setObjectName("batchOutputFolder");
    form->addRow("Image folder", pathField(folder, &dialog, [&dialog, folder] {
        const QString path = QFileDialog::getExistingDirectory(&dialog, "Input Folder", folder->text());
        if (!path.isEmpty()) folder->setText(path);
    }));
    form->addRow("Output folder", pathField(destination, &dialog, [&dialog, destination] {
        const QString path = QFileDialog::getExistingDirectory(&dialog, "Output Folder", destination->text());
        if (!path.isEmpty()) destination->setText(path);
    }));
    const auto profile = currentProfile();
    QStringList adjustments;
    if (profile.normalizeEnabled) adjustments.append("Normalize intensity");
    adjustments.append(profile.grayscaleEnabled ? "Grayscale" : "Original channels");
    if (profile.claheEnabled) adjustments.append("CLAHE");
    adjustments.append(QString("Blur %1").arg(profile.blurStrength));
    adjustments.append(QString("Scale %1%").arg(profile.resizeEnabled ? profile.resizePercent : 100));
    if (profile.autoCropEnabled) adjustments.append(QString("Trim margin %1 px").arg(profile.cropMarginPixels));
    auto *summary = new QLabel("PNG | " + adjustments.join(" | "), &dialog);
    summary->setWordWrap(true);
    form->addRow("Current settings", summary);
    layout->addLayout(form);
    auto *error = new QLabel(&dialog);
    error->setWordWrap(true);
    error->setStyleSheet("color: #914d16;");
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Export folder");
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    layout->addWidget(buttons);
    QStringList files;
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        files = ProcessingJobs::imageFiles(folder->text());
        if (files.isEmpty()) { error->setText("Choose a folder containing supported images."); return; }
        if (destination->text().trimmed().isEmpty()) { error->setText("Choose an output folder."); return; }
        dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) startJob(false, files, destination->text());
}

void MainWindow::startJob(bool crop, const QStringList &files, const QString &path, int margin, double threshold)
{
    if (m_busy) return;
    m_jobCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_jobCancel;
    const auto profile = currentProfile();
    m_jobWatcher = new QFutureWatcher<ProcessingJobResult>(this);
    auto *watcher = m_jobWatcher;
    connect(watcher, &QFutureWatcherBase::progressRangeChanged, this, [this](int minimum, int maximum) {
        m_progress->setRange(minimum, maximum);
    });
    connect(watcher, &QFutureWatcherBase::progressValueChanged, this, [this, watcher](int value) {
        m_progress->setRange(0, watcher->progressMaximum()); m_progress->setValue(value);
    });
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, crop] {
        const auto result = watcher->result();
        m_jobWatcher = nullptr;
        watcher->deleteLater();
        setJobBusy(false);
        if (!result.fatalError.isEmpty() && !m_closeWhenIdle && !QApplication::activeModalWidget()) {
            QMessageBox::warning(this, crop ? "Crop Model Failed" : "Batch Export Failed", result.fatalError);
        } else if (crop && !result.canceled && !result.session.items.isEmpty()) {
            showReviewSession(result.session);
        }
        const QString summary = !result.fatalError.isEmpty() ? "Processing failed: " + result.fatalError : QString("%1: %2 completed, %3 failed%4")
            .arg(result.canceled ? "Canceled" : (crop ? "Crop predictions ready" : "Batch export complete"))
            .arg(result.succeeded).arg(result.failed)
            .arg(result.outputFolder.isEmpty() ? QString() : " | " + result.outputFolder);
        m_statusLabel->setText(summary);
        m_statusLabel->setToolTip(summary);
        if (!result.errors.isEmpty() && !m_closeWhenIdle && !QApplication::activeModalWidget()) {
            QMessageBox report(QMessageBox::Warning, "Processing Report", summary, QMessageBox::Ok, this);
            report.setDetailedText(result.errors.join('\n'));
            report.exec();
        }
        if (m_closeWhenIdle) QTimer::singleShot(0, this, &QWidget::close);
    });
    setJobBusy(true, crop ? "Applying crop model..." : "Exporting processed images...");
    watcher->setFuture(QtConcurrent::run([=](QPromise<ProcessingJobResult> &promise) {
        promise.setProgressRange(0, static_cast<int>(files.size()));
        const auto progress = [&promise, cancel](int value, int) {
            promise.setProgressValue(value);
            return !cancel->load();
        };
        promise.addResult(crop ? ProcessingJobs::createReview(files, path, margin, threshold, progress)
                               : ProcessingJobs::preprocess(files, path, profile, progress));
    }));
}

void MainWindow::setJobBusy(bool busy, const QString &message)
{
    m_busy = busy;
    m_progress->setVisible(busy);
    m_cancelButton->setVisible(busy);
    m_cancelButton->setEnabled(busy);
    if (busy) { m_progress->setRange(0, 0); if (!message.isEmpty()) m_statusLabel->setText(message); }
    updateActions();
}

void MainWindow::cancelJob()
{
    if (m_jobCancel) m_jobCancel->store(true);
    m_review->cancelExport();
    m_profiles->cancelJob();
    m_cancelButton->setEnabled(false);
    m_statusLabel->setText("Canceling after the current image...");
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_busy) {
        const auto answer = QMessageBox::question(this, "Processing in Progress", "Cancel the running job and close?",
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) { m_closeWhenIdle = false; event->ignore(); return; }
        if (m_busy) {
            m_closeWhenIdle = true;
            cancelJob();
            event->ignore();
            return;
        }
    }
    m_closeWhenIdle = false;
    if (!m_review->maybeSave()) { event->ignore(); return; }
    if (!m_profiles->maybeSave()) { event->ignore(); return; }
    QSettings().setValue("window/geometry", saveGeometry());
    QSettings().setValue("window/preparationPanes", m_preparationSplitter->saveState());
    QSettings().setValue("window/reviewPanes", m_review->findChild<QSplitter *>("reviewSplitter")->saveState());
    QSettings().setValue("window/wormProfilePanes", m_profiles->findChild<QSplitter *>("wormProfileSplitter")->saveState());
    event->accept();
}
