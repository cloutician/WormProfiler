#include "CropReviewWorkspace.h"
#include "CropReviewSession.h"
#include "PreprocessingProfile.h"
#include "mainwindow.h"
#include "DesktopStyle.h"
#include "DesktopIcons.h"
#include "ImageViewer.h"
#include "WormProfile.h"
#include "WormProfileWorkspace.h"

#include <QCheckBox>
#include <QCalendarWidget>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QApplication>
#include <QAction>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QSlider>
#include <QLineEdit>
#include <QSettings>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace {
CropPreviewItem makeItem(const QString &path, CropReviewState state = CropReviewState::Pending)
{
    CropPreviewItem item;
    item.imagePath = path;
    item.imageSize = QSize(200, 140);
    item.cropRect = QRect(20, 25, 150, 90);
    item.originalCropRect = QRect(15, 20, 160, 100);
    item.predictionFound = true;
    item.statusText = "Model crop ready.";
    item.reviewState = state;
    return item;
}

bool writeImage(const QString &path, bool sixteenBit = false)
{
    cv::Mat image(140, 200, sixteenBit ? CV_16UC1 : CV_8UC3,
                  sixteenBit ? cv::Scalar(12000) : cv::Scalar(225, 232, 225));
    cv::ellipse(image, cv::Point(100, 70), cv::Size(65, 24), 12, 0, 360,
                sixteenBit ? cv::Scalar(48000) : cv::Scalar(85, 105, 75), -1);
    return cv::imwrite(path.toStdString(), image);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

bool writeJson(const QString &path, const QJsonObject &object)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    return file.write(QJsonDocument(object).toJson()) > 0;
}

bool writeTiff(const QString &path, const cv::Mat &image)
{
    std::vector<uchar> bytes;
    if (!cv::imencode(".tiff", image, bytes)) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()) == static_cast<qint64>(bytes.size());
}

cv::Mat tissueLabels()
{
    cv::Mat labels(160, 260, CV_8UC1, cv::Scalar(3));
    cv::ellipse(labels, cv::Point(130, 80), cv::Size(104, 40), -8, 0, 360, cv::Scalar(2), -1);
    cv::ellipse(labels, cv::Point(90, 85), cv::Size(31, 22), -8, 0, 360, cv::Scalar(1), -1);
    cv::circle(labels, cv::Point(210, 61), 5, cv::Scalar(4), -1);
    cv::circle(labels, cv::Point(213, 78), 5, cv::Scalar(4), -1);
    return labels;
}

QMap<int, TissueClass> tissueMapping()
{
    return {{1, TissueClass::Donor}, {2, TissueClass::Host}, {3, TissueClass::Background}, {4, TissueClass::Eyes}};
}
}

class ReviewWorkflowTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void profileRoundTripAndValidation();
    void profileMenuActionsRestoreControls();
    void sessionRoundTripAndValidation();
    void approvedExportPreservesPixels();
    void exportHandlesCollisionsAndFailures();
    void exportCancellationIsRecorded();
    void floatingPointSourceIsNotSilentlyConverted();
    void reviewDecisionsFiltersAndEditing();
    void missingSourceCannotBeApproved();
    void sessionSaveAndCloseProtection();
    void desktopWorkspacesPreserveState();
    void modernControlsPreserveInputAndScale();
    void searchIconsRemainCentered();
    void backgroundBatchUsesCapturedSettings();
    void approvedExportRunsInBackground();
    void cropCoordinatesRemainCorrectWhenZoomed();
    void browserHandlesFolderChangesAndUnreadableImages();
    void reviewSetupUsesOneConfigurationDialog();
    void closingDuringExportCancelsAndCloses();
    void labelTiffMeasurementsAreExact();
    void probabilityAndStackTiffsAreRejected();
    void wormProfileRoundTripAndCsv();
    void wormProfileWorkspaceWorkflow();
    void measurementExportRevalidatesSources();
    void wormProfileCloseProtection();

private:
    QTemporaryDir m_settingsDirectory;
};

void ReviewWorkflowTests::initTestCase()
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef Q_OS_WIN
    // The offscreen Windows platform does not discover system fonts automatically.
    const QString fontPath = QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/segoeui.ttf");
    QVERIFY(QFontDatabase::addApplicationFont(fontPath) >= 0);
    QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/segoeuib.ttf"));
    QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/seguisb.ttf"));
    QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/SegUIVar.ttf"));
    QApplication::setFont(QFont("Segoe UI", 9));
#endif
    configureDesktopApplication(*qApp);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
}

void ReviewWorkflowTests::profileMenuActionsRestoreControls()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PreprocessingProfile profile;
    profile.blurStrength = 4;
    profile.resizePercent = 70;
    profile.normalizeEnabled = false;
    profile.grayscaleEnabled = false;
    profile.cropMarginPixels = 65;
    QString error;
    const QString inputPath = directory.filePath("input-profile.json");
    const QString outputPath = directory.filePath("saved-profile.json");
    QVERIFY(profile.save(inputPath, &error));
    MainWindow window;
    const auto selectFile = [](const QString &path) {
        QTimer::singleShot(0, [path] {
            if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                dialog->selectFile(path);
                QMetaObject::invokeMethod(dialog, "accept");
            }
        });
    };
    selectFile(inputPath);
    QVERIFY(QMetaObject::invokeMethod(&window, "loadPreprocessingProfile"));
    QCOMPARE(window.findChild<QSlider *>("blurSlider")->value(), 4);
    QCOMPARE(window.findChild<QSlider *>("resizeSlider")->value(), 70);
    QVERIFY(!window.findChild<QCheckBox *>("normalizeCheckBox")->isChecked());
    QVERIFY(!window.findChild<QCheckBox *>("grayscaleCheckBox")->isChecked());
    QCOMPARE(window.findChild<QSpinBox *>("cropMarginSpinBox")->value(), 65);
    selectFile(outputPath);
    QVERIFY(QMetaObject::invokeMethod(&window, "savePreprocessingProfile"));
    PreprocessingProfile saved;
    QVERIFY2(PreprocessingProfile::load(outputPath, &saved, &error), qPrintable(error));
    QCOMPARE(saved.toJson(), profile.toJson());
}

void ReviewWorkflowTests::profileRoundTripAndValidation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PreprocessingProfile profile;
    profile.blurStrength = 7;
    profile.normalizeEnabled = false;
    profile.resizeEnabled = false;
    profile.resizePercent = 80;
    profile.grayscaleEnabled = false;
    profile.claheEnabled = true;
    profile.autoCropEnabled = true;
    profile.cropMarginPixels = 123;
    const QString path = directory.filePath("profile.json");
    QString error;
    QVERIFY2(profile.save(path, &error), qPrintable(error));
    PreprocessingProfile loaded;
    QVERIFY2(PreprocessingProfile::load(path, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.toJson(), profile.toJson());

    QJsonObject invalid = profile.toJson();
    invalid["resizePercent"] = 0;
    QVERIFY(!PreprocessingProfile::fromJson(invalid, &loaded, &error));
    QCOMPARE(loaded.toJson(), profile.toJson());
    invalid = profile.toJson();
    invalid["normalizeEnabled"] = "true";
    QVERIFY(!PreprocessingProfile::fromJson(invalid, &loaded, &error));
    invalid = profile.toJson();
    invalid["version"] = 2;
    QVERIFY(!PreprocessingProfile::fromJson(invalid, &loaded, &error));
}

void ReviewWorkflowTests::sessionRoundTripAndValidation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkdir("images"));
    CropReviewSession session;
    session.modelPath = directory.filePath("model.onnx");
    session.cropMarginPixels = 40;
    session.predictionThreshold = 0.72;
    for (int index = 0; index < 4; ++index) {
        session.items.append(makeItem(directory.filePath(QString("images/image-%1.png").arg(index)),
                                      static_cast<CropReviewState>(index)));
    }
    session.items[1].predictionFound = false;
    session.items[1].statusText = "No confident worm region found; using the full image.";
    session.currentIndex = 2;
    const QString path = directory.filePath("review.wormreview.json");
    QString error;
    QVERIFY2(session.save(path, &error), qPrintable(error));
    QJsonObject document = QJsonDocument::fromJson(readFile(path)).object();
    QVERIFY(document["images"].toArray()[0].toObject()["imagePath"].toString().startsWith("images/"));
    CropReviewSession loaded;
    QVERIFY2(CropReviewSession::load(path, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.items.size(), 4);
    QCOMPARE(loaded.currentIndex, 2);
    QCOMPARE(loaded.modelPath, session.modelPath);
    QCOMPARE(loaded.cropMarginPixels, 40);
    QCOMPARE(loaded.predictionThreshold, 0.72);
    for (int index = 0; index < 4; ++index) {
        QCOMPARE(loaded.items[index].imagePath, session.items[index].imagePath);
        QCOMPARE(loaded.items[index].cropRect, session.items[index].cropRect);
        QCOMPARE(loaded.items[index].originalCropRect, session.items[index].originalCropRect);
        QCOMPARE(loaded.items[index].reviewState, session.items[index].reviewState);
        QCOMPARE(loaded.items[index].statusText, session.items[index].statusText);
    }

    QJsonArray images = document["images"].toArray();
    QJsonObject image = images[0].toObject();
    QJsonObject crop = image["crop"].toObject();
    crop["width"] = 9999;
    image["crop"] = crop;
    images[0] = image;
    document["images"] = images;
    QVERIFY(writeJson(path, document));
    QVERIFY(!CropReviewSession::load(path, &loaded, &error));
    QCOMPARE(loaded.items[0].cropRect, session.items[0].cropRect);
}

void ReviewWorkflowTests::approvedExportPreservesPixels()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath("source.png");
    QVERIFY(writeImage(source, true));
    const QByteArray originalBytes = readFile(source);
    CropReviewSession session;
    session.items = {makeItem(source, CropReviewState::Approved), makeItem(source, CropReviewState::Pending),
                     makeItem(source, CropReviewState::Rejected), makeItem(source, CropReviewState::NeedsReview)};
    session.items[0].statusText = "Region, \"worm\"\nchecked";
    QVERIFY(QDir(directory.path()).mkdir("output"));
    const QString destination = directory.filePath("output");
    const CropExportResult result = session.exportApproved(destination);
    QCOMPARE(result.exported, 1);
    QCOMPARE(result.failed, 0);
    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
    QCOMPARE(QDir(destination).entryList({"*.png"}, QDir::Files).size(), 1);
    const cv::Mat crop = cv::imread(QDir(destination).filePath("source_crop.png").toStdString(), cv::IMREAD_UNCHANGED);
    const cv::Mat original = cv::imread(source.toStdString(), cv::IMREAD_UNCHANGED);
    QCOMPARE(crop.type(), CV_16UC1);
    QCOMPARE(crop.cols, 150);
    QCOMPARE(crop.rows, 90);
    QCOMPARE(cv::norm(crop, original(cv::Rect(20, 25, 150, 90)), cv::NORM_INF), 0.0);
    QCOMPARE(readFile(source), originalBytes);
    const QByteArray csv = readFile(result.manifestPath);
    QVERIFY(csv.contains("\"Region, \"\"worm\"\"\nchecked\""));
    QCOMPARE(csv.count("\"not_approved\""), 3);
    QCOMPARE(csv.count("\"exported\""), 1);
}

void ReviewWorkflowTests::exportHandlesCollisionsAndFailures()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (const QString &name : {"one", "two", "out"}) QVERIFY(QDir(directory.path()).mkdir(name));
    const QString first = directory.filePath("one/image.png"), second = directory.filePath("two/image.png");
    QVERIFY(writeImage(first));
    QVERIFY(writeImage(second));
    const QString existing = directory.filePath("out/image_crop.png");
    QVERIFY(writeImage(existing, true));
    const QByteArray existingBytes = readFile(existing);
    CropReviewSession session;
    session.items = {makeItem(first, CropReviewState::Approved), makeItem(second, CropReviewState::Approved),
                     makeItem(directory.filePath("missing.png"), CropReviewState::Approved)};
    CropPreviewItem changed = makeItem(first, CropReviewState::Approved);
    changed.imageSize = QSize(201, 140);
    session.items.append(changed);
    CropExportResult result = session.exportApproved(directory.filePath("out"));
    QCOMPARE(result.exported, 2);
    QCOMPARE(result.failed, 2);
    QCOMPARE(readFile(existing), existingBytes);
    QVERIFY(QFileInfo::exists(directory.filePath("out/image_crop_2.png")));
    QVERIFY(QFileInfo::exists(directory.filePath("out/image_crop_3.png")));
    QCOMPARE(readFile(result.manifestPath).count("\"failed\""), 2);
    const QString firstManifest = result.manifestPath;
    result = session.exportApproved(directory.filePath("out"));
    QVERIFY(result.manifestPath != firstManifest);
    QVERIFY(QFileInfo::exists(firstManifest));
}

void ReviewWorkflowTests::exportCancellationIsRecorded()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath("image.png");
    QVERIFY(writeImage(source));
    CropReviewSession session;
    session.items = {makeItem(source, CropReviewState::Approved), makeItem(source, CropReviewState::Approved)};
    const CropExportResult result = session.exportApproved(directory.path(), [](int index, int) { return index < 1; });
    QVERIFY(result.canceled);
    QCOMPARE(result.exported, 1);
    QCOMPARE(readFile(result.manifestPath).count("\"canceled\""), 1);
}

void ReviewWorkflowTests::floatingPointSourceIsNotSilentlyConverted()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath("float.tiff");
    QVERIFY(cv::imwrite(source.toStdString(), cv::Mat(140, 200, CV_32FC1, cv::Scalar(0.75))));
    CropReviewSession session;
    session.items = {makeItem(source, CropReviewState::Approved)};
    const CropExportResult result = session.exportApproved(directory.path());
    QCOMPARE(result.exported, 0);
    QCOMPARE(result.failed, 1);
    QVERIFY(result.errors.join('\n').contains("not converted"));
    QVERIFY(QDir(directory.path()).entryList({"*.png"}, QDir::Files).isEmpty());
}

void ReviewWorkflowTests::reviewDecisionsFiltersAndEditing()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath("worm.png");
    QVERIFY(writeImage(source));
    CropReviewSession session;
    session.items = {makeItem(source), makeItem(source), makeItem(source, CropReviewState::Rejected)};
    session.items[1].predictionFound = false;
    session.items[1].statusText = "No confident worm region found; using the full image.";
    session.items[1].reviewState = CropReviewState::NeedsReview;
    MainWindow window;
    CropReviewWorkspace dialog(session, &window);
    dialog.resize(1024, 768);
    dialog.show();
    QTest::qWait(30);
    auto *approve = dialog.findChild<QPushButton *>("approveCropButton");
    auto *exportButton = dialog.exportAction();
    auto *filter = dialog.findChild<QComboBox *>("reviewFilter");
    auto *list = dialog.findChild<QListWidget *>("reviewImageList");
    auto *width = dialog.findChild<QSpinBox *>("cropWidth");
    QVERIFY(approve && exportButton && filter && list && width);
    QVERIFY(!exportButton->isEnabled());
    approve->click();
    QVERIFY(exportButton->isEnabled());
    QVERIFY(list->item(0)->text().contains("Approved"));
    width->setValue(120);
    QVERIFY(!exportButton->isEnabled());
    QVERIFY(list->item(0)->text().contains("Pending"));
    approve->click();
    filter->setCurrentIndex(5);
    QVERIFY(list->item(0)->isHidden());
    QVERIFY(!list->item(1)->isHidden());
    QCOMPARE(list->currentRow(), 1);
    QVERIFY(dialog.findChild<QLabel *>("predictionStatus")->text().contains("No confident worm"));
    filter->setCurrentIndex(1);
    QCOMPARE(list->currentRow(), -1);
    QVERIFY(!approve->isEnabled());
    filter->setCurrentIndex(0);
    list->setCurrentRow(1);
    dialog.findChild<QCheckBox *>("advanceAfterDecision")->setChecked(true);
    approve->click();
    QCOMPARE(list->currentRow(), 2);
    QVERIFY(list->item(1)->text().contains("Approved"));

    const QString artifacts = qEnvironmentVariable("WORMPROFILER_TEST_ARTIFACT_DIR");
    if (!artifacts.isEmpty()) {
        QVERIFY(QDir().mkpath(artifacts));
        dialog.findChild<QCheckBox *>("advanceAfterDecision")->setChecked(false);
        list->setCurrentRow(0);
        width->setValue(150);
        approve->click();
        QTest::qWait(20);
        QVERIFY(dialog.grab().save(QDir(artifacts).filePath("crop-review-1024.png")));
        dialog.resize(1380, 900);
        QTest::qWait(20);
        QVERIFY(dialog.grab().save(QDir(artifacts).filePath("crop-review-1380.png")));
    }
    // Let destruction clean up; closing a dirty dialog correctly asks about saving.
}

void ReviewWorkflowTests::missingSourceCannotBeApproved()
{
    CropReviewSession session;
    session.items = {makeItem("C:/missing-wormprofiler-test-image.png")};
    CropReviewWorkspace dialog(session);
    QVERIFY(!dialog.findChild<QPushButton *>("approveCropButton")->isEnabled());
    QVERIFY(!dialog.findChild<QSpinBox *>("cropWidth")->isEnabled());
    QVERIFY(dialog.findChild<QLabel *>("predictionStatus")->text().contains("missing", Qt::CaseInsensitive));
}

void ReviewWorkflowTests::sessionSaveAndCloseProtection()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath("worm.png");
    const QString sessionPath = directory.filePath("review.json");
    QVERIFY(writeImage(source));
    CropReviewSession session;
    session.items = {makeItem(source)};
    MainWindow window;
    window.showReviewSession(session, sessionPath);
    window.show();
    auto *review = window.findChild<CropReviewWorkspace *>();
    review->findChild<QPushButton *>("approveCropButton")->click();
    review->saveSession();
    QVERIFY(window.windowTitle().contains("review.json"));
    CropReviewSession saved;
    QString error;
    QVERIFY2(CropReviewSession::load(sessionPath, &saved, &error), qPrintable(error));
    QCOMPARE(saved.items[0].reviewState, CropReviewState::Approved);
    review->findChild<QPushButton *>("rejectCropButton")->click();
    QTimer::singleShot(0, [] {
        auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (message) message->button(QMessageBox::Cancel)->click();
    });
    window.close();
    QVERIFY(window.isVisible());
    QTimer::singleShot(0, [] {
        auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (message) message->button(QMessageBox::Save)->click();
    });
    window.close();
    QVERIFY(!window.isVisible());
    QVERIFY(CropReviewSession::load(sessionPath, &saved, &error));
    QCOMPARE(saved.items[0].reviewState, CropReviewState::Rejected);
}

void ReviewWorkflowTests::desktopWorkspacesPreserveState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = directory.filePath("sample-01.png"), second = directory.filePath("sample-02.png");
    QVERIFY(writeImage(first)); QVERIFY(writeImage(second, true));
    MainWindow window;
    window.resize(1400, 900);
    window.setImageFiles({first, second}, directory.path());
    window.show();
    QTest::qWait(40);
    auto *tabs = window.findChild<QTabWidget *>("workspaces");
    auto *original = window.findChild<ImageViewer *>("originalImageView");
    auto *processed = window.findChild<ImageViewer *>("processedImageView");
    auto *blur = window.findChild<QSlider *>("blurSlider");
    QVERIFY(tabs && original && processed && blur);
    QCOMPARE(tabs->count(), 3);
    QVERIFY(original->hasImage()); QVERIFY(processed->hasImage());
    original->actualSize();
    QCOMPARE(original->zoomFactor(), 1.0);
    original->fitImage();
    blur->setValue(3);
    const QString artifacts = qEnvironmentVariable("WORMPROFILER_TEST_ARTIFACT_DIR");
    if (!artifacts.isEmpty()) {
        QVERIFY(window.grab().save(QDir(artifacts).filePath("desktop-preparation-1400.png")));
        window.resize(1024, 768);
        QTest::qWait(30);
        QVERIFY(window.grab().save(QDir(artifacts).filePath("desktop-preparation-1024.png")));
        window.resize(1400, 900);
    }
    CropReviewSession session;
    session.modelPath = directory.filePath("worm-model.onnx");
    session.cropMarginPixels = 40;
    session.items = {makeItem(first), makeItem(second)};
    session.items[1].predictionFound = false;
    session.items[1].reviewState = CropReviewState::NeedsReview;
    session.items[1].statusText = "No confident worm region found; using the full image.";
    window.showReviewSession(session);
    auto *review = window.findChild<CropReviewWorkspace *>();
    QVERIFY(review && !review->isWindow());
    QCOMPARE(tabs->currentIndex(), 1);
    review->findChild<QPushButton *>("approveCropButton")->click();
    QVERIFY(tabs->tabText(1).contains('*'));
    tabs->setCurrentIndex(0);
    QCOMPARE(blur->value(), 3);
    QVERIFY(original->hasImage());
    tabs->setCurrentIndex(1);
    QVERIFY(review->findChild<QListWidget *>("reviewImageList")->item(0)->text().contains("Approved"));
    if (!artifacts.isEmpty()) {
        QTest::qWait(30);
        QVERIFY(window.grab().save(QDir(artifacts).filePath("desktop-review-1400.png")));
        window.resize(1024, 768);
        QTest::qWait(30);
        QVERIFY(window.grab().save(QDir(artifacts).filePath("desktop-review-1024.png")));
        window.resize(960, 640);
        QTest::qWait(30);
        QVERIFY(window.grab().save(QDir(artifacts).filePath("desktop-review-960.png")));
        auto *approve = review->findChild<QPushButton *>("approveCropButton");
        QVERIFY(window.rect().contains(approve->mapTo(&window, QPoint(0, 0))));
        QVERIFY(window.rect().contains(approve->mapTo(&window, approve->rect().bottomRight())));
    }
    QVERIFY(QApplication::activeModalWidget() == nullptr);
}

void ReviewWorkflowTests::searchIconsRemainCentered()
{
    QTemporaryDir directory;
    const QString first = directory.filePath("sample-01.png"), second = directory.filePath("sample-02.png");
    QVERIFY(writeImage(first)); QVERIFY(writeImage(second));
    MainWindow window;
    window.resize(1024, 768);
    window.setImageFiles({first, second}, directory.path());
    window.show();
    auto *search = window.findChild<QLineEdit *>("imageSearch");
    auto *action = window.findChild<QAction *>("imageSearchAction");
    auto *list = window.findChild<QListWidget *>("preparationImageList");
    QVERIFY(search && action && list);
    QToolButton *icon = nullptr, *clear = nullptr;
    for (auto *button : search->findChildren<QToolButton *>()) {
        if (button->defaultAction() == action) icon = button;
        else clear = button;
    }
    QVERIFY(icon && clear);
    const auto verifyCentered = [search](QToolButton *button) {
        const QRect geometry = button->geometry();
        QVERIFY(search->rect().contains(geometry));
        QVERIFY(qAbs(QRectF(geometry).center().y() - QRectF(search->rect()).center().y()) <= 0.5);
    };
    window.findChild<QSlider *>("blurSlider")->setFocus();
    QTest::qWait(20);
    verifyCentered(icon);
    const QRect restingGeometry = icon->geometry();
    const QString artifacts = qEnvironmentVariable("WORMPROFILER_TEST_ARTIFACT_DIR");
    if (!artifacts.isEmpty()) QVERIFY(search->grab().save(QDir(artifacts).filePath("search-icon-unfocused.png")));
    search->setFocus();
    QTest::qWait(20);
    verifyCentered(icon);
    QCOMPARE(icon->geometry(), restingGeometry);
    if (!artifacts.isEmpty()) QVERIFY(search->grab().save(QDir(artifacts).filePath("search-icon-focused.png")));
    QTest::keyClicks(search, "sample-02");
    QTRY_VERIFY(clear->isVisible());
    verifyCentered(icon);
    verifyCentered(clear);
    QVERIFY(list->item(0)->isHidden());
    QVERIFY(!list->item(1)->isHidden());
    if (!artifacts.isEmpty()) QVERIFY(search->grab().save(QDir(artifacts).filePath("search-icon-text.png")));
    QTest::mouseClick(clear, Qt::LeftButton);
    QTRY_VERIFY(search->text().isEmpty());
    QVERIFY(!list->item(0)->isHidden());
    verifyCentered(icon);
    window.resize(1400, 900);
    QTest::qWait(20);
    verifyCentered(icon);
}

void ReviewWorkflowTests::modernControlsPreserveInputAndScale()
{
    using S = DesktopIcons::Symbol;
    for (int index = 0; index <= static_cast<int>(S::Calendar); ++index) {
        const QIcon icon = DesktopIcons::icon(static_cast<S>(index));
        QVERIFY(!icon.isNull());
        for (qreal scale : {1.0, 1.5, 2.0}) {
            const QPixmap pixmap = icon.pixmap(QSize(18, 18), scale);
            QCOMPARE(pixmap.size(), QSize(qRound(18 * scale), qRound(18 * scale)));
            QCOMPARE(pixmap.devicePixelRatio(), scale);
            const QImage pixels = pixmap.toImage();
            bool visible = false;
            for (int y = 0; y < pixels.height(); ++y) {
                for (int x = 0; x < pixels.width(); ++x) visible |= qAlpha(pixels.pixel(x, y)) > 100;
            }
            QVERIFY2(visible, qPrintable(QString("Empty icon %1 at scale %2").arg(index).arg(scale)));
        }
    }
    const QIcon checkIcon = DesktopIcons::icon(S::Check);
    QVERIFY(checkIcon.pixmap(18, 18, QIcon::Normal).toImage() != checkIcon.pixmap(18, 18, QIcon::Disabled).toImage());
    QVERIFY(checkIcon.pixmap(18, 18).toImage() != DesktopIcons::icon(S::Check, true).pixmap(18, 18).toImage());

    QWidget gallery;
    gallery.setWindowTitle("Desktop control verification");
    auto *layout = new QGridLayout(&gallery);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setVerticalSpacing(14);
    auto *title = new QLabel("Desktop controls", &gallery);
    title->setObjectName("panelTitle");
    layout->addWidget(title, 0, 0, 1, 3);
    auto *slider = new QSlider(Qt::Horizontal, &gallery);
    slider->setRange(0, 100);
    slider->setValue(35);
    slider->setMinimumWidth(260);
    layout->addWidget(new QLabel("Smoothing", &gallery), 1, 0);
    layout->addWidget(slider, 1, 1, 1, 2);
    auto *disabledSlider = new QSlider(Qt::Horizontal, &gallery);
    disabledSlider->setValue(45);
    disabledSlider->setEnabled(false);
    layout->addWidget(new QLabel("Disabled", &gallery), 2, 0);
    layout->addWidget(disabledSlider, 2, 1, 1, 2);
    auto *first = new QCheckBox("Normalize intensity", &gallery);
    auto *second = new QCheckBox("Grayscale", &gallery);
    layout->addWidget(first, 3, 1, 1, 2);
    layout->addWidget(second, 4, 1, 1, 2);
    auto *disabledCheck = new QCheckBox("Mask overlay", &gallery);
    disabledCheck->setChecked(true);
    disabledCheck->setEnabled(false);
    layout->addWidget(disabledCheck, 5, 1, 1, 2);
    auto *radio = new QRadioButton("Original channels", &gallery);
    auto *otherRadio = new QRadioButton("Single channel", &gallery);
    layout->addWidget(radio, 6, 1, 1, 2);
    layout->addWidget(otherRadio, 7, 1, 1, 2);
    auto *spin = new QSpinBox(&gallery);
    spin->setRange(0, 1000);
    spin->setValue(50);
    spin->setSuffix(" px");
    layout->addWidget(new QLabel("Margin", &gallery), 8, 0);
    layout->addWidget(spin, 8, 1);
    auto *combo = new QComboBox(&gallery);
    combo->addItems({"All images", "Pending", "Approved"});
    layout->addWidget(combo, 8, 2);
    auto *approve = new QPushButton(DesktopIcons::icon(S::Check, true), "Approve", &gallery);
    approve->setProperty("primary", true);
    auto *reject = new QPushButton(DesktopIcons::icon(S::Close), "Reject", &gallery);
    layout->addWidget(approve, 9, 1);
    layout->addWidget(reject, 9, 2);
    gallery.resize(620, 550);
    gallery.show();
    QTest::qWait(20);

    slider->setFocus();
    QTest::keyClick(slider, Qt::Key_Right);
    QCOMPARE(slider->value(), 36);
    QTest::keyClick(slider, Qt::Key_End);
    QCOMPARE(slider->value(), 100);
    QTest::keyClick(slider, Qt::Key_Home);
    QCOMPARE(slider->value(), 0);
    QTest::mouseClick(slider, Qt::LeftButton, Qt::NoModifier, QPoint(slider->width() * 3 / 4, slider->height() / 2));
    QVERIFY(slider->value() > 70 && slider->value() < 85);
    QStyleOptionSlider sliderOption;
    sliderOption.initFrom(slider);
    sliderOption.orientation = Qt::Horizontal;
    sliderOption.minimum = 0;
    sliderOption.maximum = 100;
    sliderOption.sliderPosition = slider->value();
    const QRect handle = slider->style()->subControlRect(QStyle::CC_Slider, &sliderOption, QStyle::SC_SliderHandle, slider);
    QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, handle.center());
    QTest::mouseMove(slider, QPoint(slider->width() - 11, handle.center().y()));
    QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, QPoint(slider->width() - 11, handle.center().y()));
    QCOMPARE(slider->value(), 100);
    const QSize sliderSize = slider->size();
    disabledSlider->setFocus();
    QTest::keyClick(disabledSlider, Qt::Key_Right);
    QCOMPARE(disabledSlider->value(), 45);
    QTest::mouseClick(first, Qt::LeftButton, Qt::NoModifier, QPoint(10, first->height() / 2));
    second->setFocus();
    QTest::keyClick(second, Qt::Key_Space);
    QVERIFY(first->isChecked() && second->isChecked());
    QTest::mouseClick(radio, Qt::LeftButton, Qt::NoModifier, QPoint(10, radio->height() / 2));
    otherRadio->setFocus();
    QTest::keyClick(otherRadio, Qt::Key_Space);
    QVERIFY(!radio->isChecked() && otherRadio->isChecked());
    QVERIFY(first->isChecked() && second->isChecked());

    QStyleOptionSpinBox spinOption;
    spinOption.initFrom(spin);
    spinOption.buttonSymbols = QAbstractSpinBox::UpDownArrows;
    spinOption.stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
    const QRect up = spin->style()->subControlRect(QStyle::CC_SpinBox, &spinOption, QStyle::SC_SpinBoxUp, spin);
    const QRect down = spin->style()->subControlRect(QStyle::CC_SpinBox, &spinOption, QStyle::SC_SpinBoxDown, spin);
    QVERIFY(!up.isEmpty() && !down.isEmpty() && !up.intersects(down));
    QTest::mouseClick(spin, Qt::LeftButton, Qt::NoModifier, up.center());
    QCOMPARE(spin->value(), 51);
    QTest::mouseClick(spin, Qt::LeftButton, Qt::NoModifier, down.center());
    QCOMPARE(spin->value(), 50);
    combo->setFocus();
    QTest::keyClick(combo, Qt::Key_Down);
    QCOMPARE(combo->currentIndex(), 1);
    QCOMPARE(slider->size(), sliderSize);
    slider->setValue(35);
    second->setChecked(false);
    otherRadio->setChecked(false);
    radio->setChecked(true);
    approve->setFocus();
    QTest::qWait(20);
    const QString artifacts = qEnvironmentVariable("WORMPROFILER_TEST_ARTIFACT_DIR");
    if (!artifacts.isEmpty()) QVERIFY(gallery.grab().save(QDir(artifacts).filePath("desktop-controls.png")));
}

void ReviewWorkflowTests::backgroundBatchUsesCapturedSettings()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath("source.png");
    QVERIFY(writeImage(source));
    const QByteArray sourceBytes = readFile(source);
    const QString output = directory.filePath("output");
    MainWindow window;
    window.setImageFiles({source}, directory.path());
    CropReviewSession reviewSession;
    reviewSession.items = {makeItem(source, CropReviewState::Approved)};
    window.showReviewSession(reviewSession);
    window.findChild<QTabWidget *>("workspaces")->setCurrentIndex(0);
    auto *review = window.findChild<CropReviewWorkspace *>();
    window.findChild<QSlider *>("resizeSlider")->setValue(70);
    QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        dialog->findChild<QLineEdit *>("batchInputFolder")->setText(directory.path());
        dialog->findChild<QLineEdit *>("batchOutputFolder")->setText(output);
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    QVERIFY(QMetaObject::invokeMethod(&window, "batchPreprocessFolder"));
    auto *batch = window.findChild<QAction *>("actionBatchExport");
    QVERIFY(!batch->isEnabled());
    QVERIFY(!review->exportAction()->isEnabled());
    window.findChild<QSlider *>("resizeSlider")->setValue(30);
    QTRY_VERIFY_WITH_TIMEOUT(batch->isEnabled(), 5000);
    QVERIFY(review->exportAction()->isEnabled());
    const cv::Mat image = cv::imread(QDir(output).filePath("source_processed.png").toStdString(), cv::IMREAD_UNCHANGED);
    QCOMPARE(image.cols, 140);
    QCOMPARE(image.rows, 98);
    QCOMPARE(readFile(source), sourceBytes);
    PreprocessingProfile profile;
    QString error;
    QVERIFY(PreprocessingProfile::load(QDir(output).filePath("preprocessing-profile.json"), &profile, &error));
    QCOMPARE(profile.resizePercent, 70);
    const auto again = ProcessingJobs::preprocess({source}, output, profile, {});
    QCOMPARE(again.succeeded, 1);
    QVERIFY(QFileInfo::exists(QDir(output).filePath("source_processed_2.png")));
}

void ReviewWorkflowTests::approvedExportRunsInBackground()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath("source.png");
    const QString output = directory.filePath("output");
    QVERIFY(writeImage(source));
    QVERIFY(QDir().mkpath(output));
    CropReviewSession session;
    session.items = {makeItem(source, CropReviewState::Approved), makeItem(source)};
    MainWindow window;
    window.showReviewSession(session);
    auto *review = window.findChild<CropReviewWorkspace *>();
    QTimer::singleShot(0, [&] {
        if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
            dialog->setDirectory(output);
            QMetaObject::invokeMethod(dialog, "accept");
        }
    });
    review->exportCrops();
    QVERIFY(review->isBusy());
    window.findChild<QTabWidget *>("workspaces")->setCurrentIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(!review->isBusy(), 5000);
    QCOMPARE(QDir(output).entryList({"*.png"}, QDir::Files).size(), 1);
    QVERIFY(QFileInfo::exists(QDir(output).filePath("crop_manifest.csv")));
}

void ReviewWorkflowTests::cropCoordinatesRemainCorrectWhenZoomed()
{
    CropImageView view;
    view.resize(600, 400);
    QImage image(200, 140, QImage::Format_RGB32);
    image.fill(Qt::white);
    view.setImage(image);
    view.setCropRect(QRect(20, 25, 150, 90));
    view.show();
    view.actualSize();
    QCOMPARE(view.zoomFactor(), 1.0);
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, QPoint(295, 200));
    QTest::mouseMove(&view, QPoint(315, 210));
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, QPoint(315, 210));
    QCOMPARE(view.cropRect(), QRect(40, 35, 150, 90));
    const QRect crop = view.cropRect();
    QWheelEvent wheel(QPointF(300, 200), QPointF(view.mapToGlobal(QPoint(300, 200))), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&view, &wheel);
    QVERIFY(view.zoomFactor() > 1.0);
    QCOMPARE(view.cropRect(), crop);
    QTest::mousePress(&view, Qt::MiddleButton, Qt::NoModifier, QPoint(300, 200));
    QTest::mouseMove(&view, QPoint(330, 220));
    QTest::mouseRelease(&view, Qt::MiddleButton, Qt::NoModifier, QPoint(330, 220));
    QCOMPARE(view.cropRect(), crop);
}

void ReviewWorkflowTests::browserHandlesFolderChangesAndUnreadableImages()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString bad = directory.filePath("bad.png"), first = directory.filePath("first.png"),
        second = directory.filePath("second.png");
    QFile invalid(bad);
    QVERIFY(invalid.open(QIODevice::WriteOnly));
    invalid.write("not an image");
    invalid.close();
    QVERIFY(writeImage(first)); QVERIFY(writeImage(second));
    MainWindow window;
    window.setImageFiles({bad, first}, directory.path());
    auto *list = window.findChild<QListWidget *>("preparationImageList");
    auto *view = window.findChild<ImageViewer *>("originalImageView");
    QCOMPARE(list->currentRow(), 1);
    QVERIFY(view->hasImage());
    window.findChild<QLineEdit *>("imageSearch")->setText("first");
    window.setImageFiles({second}, directory.path());
    QCOMPARE(list->currentRow(), 0);
    QVERIFY(window.windowTitle().contains("second.png"));
    QVERIFY(!window.loadImagePath(bad));
    QVERIFY(view->hasImage());
    window.setImageFiles({bad}, directory.path());
    QCOMPARE(list->currentRow(), -1);
    QVERIFY(!view->hasImage());
}

void ReviewWorkflowTests::reviewSetupUsesOneConfigurationDialog()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath("worm.png");
    QVERIFY(writeImage(source));
    MainWindow window;
    window.setImageFiles({source}, directory.path());
    bool sawSetup = false;
    QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        sawSetup = dialog->objectName() == "newReviewDialog"
            && dialog->findChild<QLineEdit *>("reviewInputFolder")
            && dialog->findChild<QLineEdit *>("reviewModelPath")
            && dialog->findChild<QSpinBox *>("reviewMargin");
        const QString artifacts = qEnvironmentVariable("WORMPROFILER_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) dialog->grab().save(QDir(artifacts).filePath("desktop-review-setup.png"));
        dialog->reject();
    });
    QVERIFY(QMetaObject::invokeMethod(&window, "applyModelCropFolder"));
    QVERIFY(sawSetup);
    QVERIFY(!window.findChild<CropReviewWorkspace *>()->hasSession());
}

void ReviewWorkflowTests::closingDuringExportCancelsAndCloses()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath("source.png");
    const QString output = directory.filePath("output");
    QVERIFY(writeImage(source));
    QVERIFY(QDir().mkpath(output));
    CropReviewSession session;
    for (int index = 0; index < 30; ++index) session.items.append(makeItem(source, CropReviewState::Approved));
    MainWindow window;
    window.showReviewSession(session);
    window.show();
    auto *review = window.findChild<CropReviewWorkspace *>();
    QTimer::singleShot(0, [&] {
        if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
            dialog->setDirectory(output);
            QMetaObject::invokeMethod(dialog, "accept");
        }
    });
    review->exportCrops();
    QTimer::singleShot(0, [] {
        if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            if (message->button(QMessageBox::Yes)) message->button(QMessageBox::Yes)->click();
        }
    });
    window.close();
    QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 5000);
    QVERIFY(!review->isBusy());
    QVERIFY(QFileInfo::exists(QDir(output).filePath("crop_manifest.csv")));
}

void ReviewWorkflowTests::labelTiffMeasurementsAreExact()
{
    QTemporaryDir directory;
    for (bool sixteenBit : {false, true}) {
        const QString path = directory.filePath(sixteenBit ? "labels-16.tiff" : "labels-8.tiff");
        cv::Mat labels(2, 5, sixteenBit ? CV_16UC1 : CV_8UC1);
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 5; ++x) {
            if (sixteenBit) labels.at<quint16>(y, x) = x == 4 ? 1000 : x;
            else labels.at<uchar>(y, x) = x;
        }
        QVERIFY(writeTiff(path, labels));
        const QByteArray originalBytes = readFile(path);
        const auto statistics = WormMeasurements::inspectTiff(path);
        QVERIFY2(statistics.valid(), qPrintable(statistics.error));
        QCOMPARE(statistics.size, QSize(5, 2));
        QCOMPARE(statistics.bitDepth, sixteenBit ? 16 : 8);
        QCOMPARE(statistics.labelPixels.size(), 5);
        QCOMPARE(statistics.labelPixels.value(sixteenBit ? 1000 : 4), quint64(2));
        WormProfile profile;
        profile.wormId = "Worm-001";
        profile.classMapping = tissueMapping();
        if (sixteenBit) { profile.classMapping.remove(4); profile.classMapping[1000] = TissueClass::Eyes; }
        profile.classMapping[0] = TissueClass::Ignore;
        WormObservation observation;
        observation.id = "day-0";
        observation.imagePath = path;
        observation.date = QDate(2026, 1, 1);
        observation.statistics = statistics;
        profile.observations.append(observation);
        const auto areas = profile.areas(observation);
        for (quint64 pixels : areas.pixels) QCOMPARE(pixels, quint64(2));
        QCOMPARE(areas.wormPixels(), quint64(6));
        QCOMPARE(areas.unmapped, quint64(0));
        QVERIFY(profile.exportProblem().isEmpty());
        QString error;
        QVERIFY(!profile.writeCsv(path, &error));
        QVERIFY(!profile.save(path, &error));
        QCOMPARE(readFile(path), originalBytes);
        const QImage preview = WormMeasurements::previewTiff(path, profile.classMapping, statistics.sha256, &error);
        QVERIFY2(!preview.isNull(), qPrintable(error));
        QCOMPARE(preview.size(), QSize(5, 2));
        QCOMPARE(preview.pixelColor(1, 0), tissueClassColor(TissueClass::Donor));
        QCOMPARE(readFile(path), originalBytes);
        profile.classMapping.remove(0);
        QCOMPARE(profile.areas(observation).unmapped, quint64(2));
        QVERIFY(!profile.writeCsv(directory.filePath("blocked.csv"), &error));
        QVERIFY(!QFileInfo::exists(directory.filePath("blocked.csv")));
        profile.classMapping[0] = TissueClass::Background;
        QCOMPARE(profile.areas(observation).pixels[2], quint64(4));
        QVERIFY(writeTiff(path, cv::Mat(2, 5, sixteenBit ? CV_16UC1 : CV_8UC1, cv::Scalar(3))));
        error.clear();
        QVERIFY(WormMeasurements::previewTiff(path, profile.classMapping, statistics.sha256, &error).isNull());
        QVERIFY(error.contains("changed"));
    }
    WormProfile cancellation;
    cancellation.wormId = "Canceled";
    for (int index = 0; index < 2; ++index) {
        WormObservation observation;
        observation.imagePath = directory.filePath("labels-8.tiff");
        cancellation.observations << observation;
    }
    QVERIFY(!WormMeasurements::measureProfile(&cancellation, [](int value, int) { return value < 1; }));
    QVERIFY(cancellation.observations[0].statistics.valid());
    QVERIFY(!cancellation.observations[1].statistics.valid());
    const QString unicodePath = directory.filePath(QString("mask-%1.tif").arg(QChar(0x03b1)));
    QVERIFY(writeTiff(unicodePath, tissueLabels()));
    QVERIFY(WormMeasurements::inspectTiff(unicodePath).valid());
}

void ReviewWorkflowTests::probabilityAndStackTiffsAreRejected()
{
    QTemporaryDir directory;
    const QString floats = directory.filePath("probabilities.tif"), rgb = directory.filePath("rendering.tif");
    QVERIFY(writeTiff(floats, cv::Mat(10, 12, CV_32FC1, cv::Scalar(0.7))));
    QVERIFY(writeTiff(rgb, cv::Mat(10, 12, CV_8UC3, cv::Scalar(1, 2, 3))));
    for (const QString &path : {floats, rgb}) {
        const auto statistics = WormMeasurements::inspectTiff(path);
        QVERIFY(!statistics.valid());
        QVERIFY(statistics.error.contains("Simple Segmentation"));
    }
    const QString stack = directory.filePath("stack.tif");
    const std::vector<cv::Mat> pages{cv::Mat(10, 12, CV_8UC1, cv::Scalar(1)), cv::Mat(10, 12, CV_8UC1, cv::Scalar(2))};
    QVERIFY(cv::imwritemulti(stack.toStdString(), pages));
    const auto statistics = WormMeasurements::inspectTiff(stack);
    QVERIFY(!statistics.valid());
    QVERIFY(statistics.error.contains("Multi-page"));
    QVERIFY(!WormMeasurements::inspectTiff(directory.filePath("missing.tif")).valid());
    const QString notTiff = directory.filePath("disguised.tif");
    QVERIFY(writeJson(notTiff, QJsonObject{{"not", "a TIFF"}}));
    QVERIFY(!WormMeasurements::inspectTiff(notTiff).valid());
}

void ReviewWorkflowTests::wormProfileRoundTripAndCsv()
{
    QTemporaryDir directory;
    const QString first = directory.filePath("day-0.tif"), second = directory.filePath("day-7.tif");
    cv::Mat labels(2, 4, CV_8UC1);
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x) labels.at<uchar>(y, x) = x + 1;
    QVERIFY(writeTiff(first, labels)); QVERIFY(writeTiff(second, labels));
    WormProfile profile;
    profile.wormId = "Worm-001";
    profile.notes = "Control animal";
    profile.classMapping = tissueMapping();
    for (const QString &path : {second, first}) {
        WormObservation observation;
        observation.id = path == first ? "baseline" : "follow-up";
        observation.imagePath = path;
        observation.date = path == first ? QDate(2026, 1, 1) : QDate(2026, 1, 8);
        if (path == first) { observation.pixelWidthUm = 0.5; observation.pixelHeightUm = 0.25; }
        profile.observations << observation;
    }
    QString error;
    const QString path = directory.filePath("worm.wormprofile.json");
    QVERIFY2(profile.save(path, &error), qPrintable(error));
    const auto object = QJsonDocument::fromJson(readFile(path)).object();
    QVERIFY(!QDir::isAbsolutePath(object["observations"].toArray()[0].toObject()["imagePath"].toString()));
    WormProfile loaded;
    QVERIFY2(WormProfile::load(path, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.wormId, profile.wormId);
    QCOMPARE(loaded.classMapping, profile.classMapping);
    QCOMPARE(loaded.observations.first().id, QString("baseline"));
    QCOMPARE(loaded.observations.first().pixelWidthUm, 0.5);
    QVERIFY(WormMeasurements::measureProfile(&loaded));
    const QString csvPath = directory.filePath("measurements.csv");
    QVERIFY2(loaded.writeCsv(csvPath, &error), qPrintable(error));
    const QString csv = QString::fromUtf8(readFile(csvPath));
    const auto lines = csv.split("\r\n");
    QCOMPARE(lines.size(), 4);
    const auto baseline = lines[1].split(','), followup = lines[2].split(',');
    QCOMPARE(baseline.size(), 26);
    QCOMPARE(baseline[2], QString("2026-01-01"));
    QCOMPARE(baseline[3], QString("0"));
    QCOMPARE(followup[3], QString("7"));
    for (int index = 9; index <= 12; ++index) QCOMPARE(baseline[index], QString("2"));
    QCOMPARE(baseline[13], QString("0"));
    QCOMPARE(baseline[15], QString("6"));
    QCOMPARE(baseline[16], QString("0.5"));
    QCOMPARE(baseline[19], QString("0.25"));
    for (int index = 17; index <= 22; ++index) QVERIFY(followup[index].isEmpty());
    loaded.wormId = "=formula";
    loaded.observations[0].notes = "comma, quote\" and\nnew line";
    QVERIFY(loaded.writeCsv(csvPath, &error));
    const auto escaped = QString::fromUtf8(readFile(csvPath));
    QVERIFY(escaped.contains("\"'=formula\""));
    QVERIFY(escaped.contains("\"comma, quote\"\" and\nnew line\""));
    loaded.observations[0].pixelHeightUm = 0;
    QVERIFY(!loaded.exportProblem().isEmpty());

    for (int invalid = 0; invalid < 4; ++invalid) {
        auto bad = object;
        auto images = bad["observations"].toArray();
        auto image = images[0].toObject();
        if (invalid == 0) image["date"] = "2026-02-31";
        if (invalid == 1) image["pixelWidthUm"] = -1;
        if (invalid == 2) image["id"] = images[1].toObject()["id"];
        images[0] = image;
        bad["observations"] = images;
        if (invalid == 3) {
            auto mapping = bad["classMapping"].toArray();
            mapping.append(mapping[0]); bad["classMapping"] = mapping;
        }
        const QString invalidPath = directory.filePath("invalid.json");
        QVERIFY(writeJson(invalidPath, bad));
        const QString existingId = loaded.wormId;
        QVERIFY(!WormProfile::load(invalidPath, &loaded, &error));
        QCOMPARE(loaded.wormId, existingId);
    }
}

void ReviewWorkflowTests::wormProfileWorkspaceWorkflow()
{
    QTemporaryDir directory;
    const QString first = directory.filePath("worm-007-day-0.tif"), second = directory.filePath("worm-007-day-7.tif");
    QVERIFY(writeTiff(first, tissueLabels())); QVERIFY(writeTiff(second, tissueLabels()));
    const QByteArray source = readFile(first);
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    auto *workspace = window.findChild<WormProfileWorkspace *>();
    auto *tabs = window.findChild<QTabWidget *>("workspaces");
    QVERIFY(workspace && tabs);
    tabs->setCurrentIndex(2);
    QVERIFY(workspace->createProfile("Worm-007"));
    QVERIFY(workspace->addImages({first, second}, QDate(2026, 1, 8)));
    QVERIFY(workspace->isBusy());
    QVERIFY(!window.findChild<QAction *>("actionBatchExport")->isEnabled());
    QVERIFY(!window.findChild<CropReviewWorkspace *>()->newAction()->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(!workspace->isBusy(), 10000);
    QCOMPARE(workspace->profile().observations.size(), 2);
    QVERIFY2(workspace->profile().observations[0].statistics.valid(), qPrintable(workspace->profile().observations[0].statistics.error));
    QVERIFY(!workspace->addImages({first}, QDate(2026, 1, 9)));
    QVERIFY(!workspace->exportAction()->isEnabled());
    const QString artifacts = qEnvironmentVariable("WORMPROFILER_TEST_ARTIFACT_DIR");
    bool classesOpened = false;
    QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "wormTissueClassesDialog") return;
        classesOpened = true;
        for (int label = 1; label <= 4; ++label) {
            auto *choice = dialog->findChild<QComboBox *>(QString("classForLabel_%1").arg(label));
            QVERIFY(choice);
            choice->setCurrentIndex(label);
        }
        QTest::qWait(20);
        for (auto *choice : dialog->findChildren<QComboBox *>()) QVERIFY(choice->width() >= 220);
        if (!artifacts.isEmpty()) QVERIFY(dialog->grab().save(QDir(artifacts).filePath("worm-tissue-classes.png")));
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    workspace->configureClasses();
    QVERIFY(classesOpened);
    QCOMPARE(workspace->profile().classMapping, tissueMapping());
    QVERIFY(workspace->exportAction()->isEnabled());
    auto *viewer = workspace->findChild<ImageViewer *>("wormSegmentationView");
    QTRY_VERIFY_WITH_TIMEOUT(viewer->hasImage(), 5000);
    auto *date = workspace->findChild<QDateEdit *>("wormObservationDate");
    QVERIFY(date && date->calendarPopup());
    QTest::mouseClick(date, Qt::LeftButton, Qt::NoModifier, QPoint(date->width() - 14, date->height() / 2));
    QTRY_VERIFY(date->calendarWidget()->isVisible());
    if (!artifacts.isEmpty()) QVERIFY(date->calendarWidget()->grab().save(QDir(artifacts).filePath("worm-date-calendar.png")));
    date->calendarWidget()->window()->hide();
    date->setDate(QDate(2026, 1, 1));
    QVERIFY(viewer->hasImage());
    QCOMPARE(workspace->profile().observations[0].date, QDate(2026, 1, 1));
    workspace->findChild<QDoubleSpinBox *>("wormPixelWidth")->setValue(0.5);
    QVERIFY(!workspace->exportAction()->isEnabled());
    workspace->findChild<QDoubleSpinBox *>("wormPixelHeight")->setValue(0.25);
    QVERIFY(workspace->exportAction()->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(viewer->hasImage(), 5000);
    auto *views = workspace->findChild<QTabWidget *>("wormProfileViews");
    auto *measurementTable = workspace->findChild<QTableWidget *>("wormMeasurementsTable");
    views->setCurrentIndex(1);
    measurementTable->setCurrentCell(0, 0);
    measurementTable->setFocus();
    QTest::keyClick(measurementTable, Qt::Key_Down);
    QCOMPARE(workspace->findChild<QListWidget *>("wormObservations")->currentRow(), 1);
    QCOMPARE(date->date(), QDate(2026, 1, 8));
    QTest::keyClick(measurementTable, Qt::Key_Up);
    QCOMPARE(date->date(), QDate(2026, 1, 1));
    views->setCurrentIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(viewer->hasImage(), 5000);
    if (!artifacts.isEmpty()) {
        QVERIFY(window.grab().save(QDir(artifacts).filePath("worm-profile-1400.png")));
        views->setCurrentIndex(1);
        QTest::qWait(20);
        QVERIFY(window.grab().save(QDir(artifacts).filePath("worm-measurements-1400.png")));
        views->setCurrentIndex(0);
        window.resize(960, 640);
        QTest::qWait(20);
        QVERIFY(window.grab().save(QDir(artifacts).filePath("worm-profile-960.png")));
    }
    const QString csvPath = directory.filePath("worm.csv");
    QVERIFY(workspace->exportCsvTo(csvPath));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace->isBusy(), 10000);
    QVERIFY(QFileInfo::exists(csvPath));
    QCOMPARE(readFile(first), source);
    const QString profilePath = directory.filePath("worm-007.wormprofile.json");
    QTimer::singleShot(0, [profilePath] {
        if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
            dialog->selectFile(profilePath); QMetaObject::invokeMethod(dialog, "accept");
        }
    });
    workspace->saveProfile();
    QVERIFY(!workspace->isDirty());
    QCOMPARE(workspace->profilePath(), profilePath);
    WormProfileWorkspace reopened;
    QString error;
    QVERIFY2(reopened.openProfileFile(profilePath, &error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!reopened.isBusy(), 10000);
    QCOMPARE(reopened.profile().observations[0].date, QDate(2026, 1, 1));
    QCOMPARE(reopened.profile().observations[0].pixelHeightUm, 0.25);
    QCOMPARE(reopened.profile().classMapping, tissueMapping());
    QVERIFY(!reopened.isDirty());
    QVERIFY(QFile::remove(first));
    reopened.reloadMeasurements();
    QTRY_VERIFY_WITH_TIMEOUT(!reopened.isBusy(), 10000);
    QVERIFY(!reopened.profile().observations[0].statistics.valid());
    QVERIFY(!reopened.exportAction()->isEnabled());
}

void ReviewWorkflowTests::measurementExportRevalidatesSources()
{
    QTemporaryDir directory;
    const QString path = directory.filePath("labels.tif"), output = directory.filePath("areas.csv");
    QVERIFY(writeTiff(path, tissueLabels()));
    WormProfileWorkspace workspace;
    QVERIFY(workspace.createProfile("Worm-008"));
    QVERIFY(workspace.addImages({path}, QDate(2026, 1, 1)));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.isBusy(), 10000);
    QVERIFY(workspace.setClassMapping(tissueMapping()));
    QTRY_VERIFY(workspace.findChild<ImageViewer *>()->hasImage());
    QVERIFY(writeTiff(path, cv::Mat(160, 260, CV_8UC1, cv::Scalar(1))));
    QVERIFY(workspace.exportCsvTo(output));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.isBusy(), 10000);
    QCOMPARE(workspace.profile().areas(workspace.profile().observations[0]).pixels[0], quint64(160 * 260));
    const QByteArray goodCsv = readFile(output);
    QVERIFY(goodCsv.contains("41600,0,0,0"));
    QTRY_VERIFY(workspace.findChild<ImageViewer *>()->hasImage());
    QVERIFY(writeTiff(path, cv::Mat(160, 260, CV_32FC1, cv::Scalar(0.7))));
    QTimer dismiss;
    connect(&dismiss, &QTimer::timeout, this, [] {
        if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) message->accept();
    });
    dismiss.start(10);
    QVERIFY(workspace.exportCsvTo(output));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.isBusy(), 10000);
    QTRY_VERIFY(QApplication::activeModalWidget() == nullptr);
    QCOMPARE(readFile(output), goodCsv);
    QVERIFY(!workspace.exportAction()->isEnabled());
}

void ReviewWorkflowTests::wormProfileCloseProtection()
{
    MainWindow window;
    window.show();
    auto *workspace = window.findChild<WormProfileWorkspace *>();
    QVERIFY(workspace->createProfile("Worm-unsaved"));
    QTimer::singleShot(0, [] {
        if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) message->button(QMessageBox::Cancel)->click();
    });
    QVERIFY(!window.close());
    QVERIFY(window.isVisible());
    QTimer::singleShot(0, [] {
        if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) message->button(QMessageBox::Discard)->click();
    });
    QVERIFY(window.close());
}

QTEST_MAIN(ReviewWorkflowTests)
#include "ReviewWorkflowTests.moc"
