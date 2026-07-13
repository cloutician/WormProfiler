#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <opencv2/opencv.hpp>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class ImageLoader;
class QLabel;
class PreprocessingPipeline;
class QResizeEvent;
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void loadMask();
    void openImage();
    void saveProcessedImage();
    void updateOverlay(); // not sure
    void batchPreprocessFolder();
    void applyModelCropFolder();
    void savePreprocessingProfile();

private:
    void displayMat(QLabel *label, const cv::Mat &mat) const;
    void refreshImagePreviews();
    void updateProcessedImage();

    Ui::MainWindow *ui;
    ImageLoader *m_imageLoader;
    PreprocessingPipeline *m_preprocessingPipeline;
    cv::Mat m_currentOriginalImage;
    cv::Mat m_currentProcessedImage;
    cv::Mat m_currentOverlayImage;
    cv::Mat m_currentMask;
    bool m_uiReady = false;
};

#endif // MAINWINDOW_H
