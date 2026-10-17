#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "PreprocessingProfile.h"
#include "ProcessingJobs.h"
#include <QMainWindow>
#include <QStringList>
#include <atomic>
#include <memory>
#include <opencv2/core.hpp>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE
class QAction;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
class QSplitter;
class QToolBar;
class QToolButton;
class QResizeEvent;
class CropReviewWorkspace;
class WormProfileWorkspace;
class ImageViewer;
template<typename T> class QFutureWatcher;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    bool loadImagePath(const QString &path);
    void setImageFiles(const QStringList &paths, const QString &folder = QString());
    void showReviewSession(const CropReviewSession &session, const QString &path = QString());

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void openImage();
    void openFolder();
    void loadMask();
    void saveProcessedImage();
    void updateOverlay();
    void updateProcessedImage();
    void batchPreprocessFolder();
    void applyModelCropFolder();
    void savePreprocessingProfile();
    void loadPreprocessingProfile();
    void openCropReviewSession();
    void updateWorkspace();
    void updateActions();
    void cancelJob();

private:
    void buildPreparationWorkspace();
    void buildMenus();
    void buildStatusBar();
    void refreshImagePreviews();
    void applyProfile(const PreprocessingProfile &profile);
    PreprocessingProfile currentProfile() const;
    void loadThumbnail(int index, int generation);
    void startJob(bool crop, const QStringList &files, const QString &path, int margin = 0, double threshold = 0.65);
    void setJobBusy(bool busy, const QString &message = QString());
    void navigateImage(int direction);

    Ui::MainWindow *ui = nullptr;
    CropReviewWorkspace *m_review = nullptr;
    WormProfileWorkspace *m_profiles = nullptr;
    QWidget *m_preparation = nullptr;
    QToolBar *m_preparationToolbar = nullptr;
    QSplitter *m_preparationSplitter = nullptr;
    QListWidget *m_imageList = nullptr;
    QLineEdit *m_imageSearch = nullptr;
    QLabel *m_folderLabel = nullptr;
    QLabel *m_documentLabel = nullptr;
    QLabel *m_originalInfo = nullptr;
    QLabel *m_processedInfo = nullptr;
    QLabel *m_processedHeading = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QToolButton *m_cancelButton = nullptr;
    ImageViewer *m_originalView = nullptr;
    ImageViewer *m_processedView = nullptr;
    QWidget *m_inspector = nullptr;
    QCheckBox *m_normalize = nullptr;
    QCheckBox *m_grayscale = nullptr;
    QCheckBox *m_clahe = nullptr;
    QSlider *m_blur = nullptr;
    QSpinBox *m_blurValue = nullptr;
    QCheckBox *m_resize = nullptr;
    QSlider *m_scale = nullptr;
    QSpinBox *m_scaleValue = nullptr;
    QCheckBox *m_autoCrop = nullptr;
    QSpinBox *m_margin = nullptr;
    QCheckBox *m_showMask = nullptr;
    QSlider *m_maskOpacity = nullptr;
    QSpinBox *m_maskOpacityValue = nullptr;
    QPushButton *m_loadMaskButton = nullptr;
    QLabel *m_maskLabel = nullptr;
    QAction *m_exportImageAction = nullptr;
    QAction *m_saveDocumentAction = nullptr;
    QAction *m_batchAction = nullptr;
    QAction *m_loadMaskAction = nullptr;
    QAction *m_previousAction = nullptr;
    QAction *m_nextAction = nullptr;
    QAction *m_fitAction = nullptr;
    QAction *m_actualSizeAction = nullptr;
    QFutureWatcher<ProcessingJobResult> *m_jobWatcher = nullptr;
    std::shared_ptr<std::atomic_bool> m_jobCancel;
    QStringList m_imagePaths;
    QString m_currentPath;
    QString m_currentFolder;
    cv::Mat m_currentOriginalImage;
    cv::Mat m_currentProcessedImage;
    cv::Mat m_currentOverlayImage;
    cv::Mat m_currentMask;
    bool m_busy = false;
    bool m_closeWhenIdle = false;
    int m_thumbnailGeneration = 0;
};

#endif
