#include "mainwindow.h"
#include "ImageLoader.h"
#include "./ui_mainwindow.h"
#include "PreprocessingPipeline.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QPixmap>
#include <QDebug>
#include <QSlider>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_imageLoader(new ImageLoader)
    , m_preprocessingPipeline(new PreprocessingPipeline)
{
    ui->setupUi(this);

    connect(ui->actionOpen_Image, &QAction::triggered, this, &MainWindow::openImage);
    connect(ui->blurSlider, &QSlider::valueChanged, this, &MainWindow::updateProcessedImage);
    connect(ui->normalizeCheckBox, &QCheckBox::toggled, this, &MainWindow::updateProcessedImage);
    connect(ui->actionSave_Processed_Image, &QAction::triggered, this, &MainWindow::saveProcessedImage);
    connect(ui->actionLoad_Mask, &QAction::triggered, this, &MainWindow::loadMask); //not sure, revisit
    connect(ui->actionBatch_Preprocess_Folder,&QAction::triggered, this, &MainWindow::batchPreprocessFolder);
    connect(ui->resizeSlider, &QSlider::valueChanged, this, &MainWindow::updateProcessedImage);
    connect(ui->resizeCheckBox, &QCheckBox::toggled, this, &MainWindow::updateProcessedImage);
    connect(ui->grayscaleCheckBox, &QCheckBox::toggled, this, &MainWindow::updateProcessedImage);
}

MainWindow::~MainWindow()
{
    delete m_preprocessingPipeline;
    delete m_imageLoader;
    delete ui;
}

void MainWindow::openImage()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "Open Image",
        "",
        "Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)"
        );

    if (fileName.isEmpty()) {
        return;
    }

    m_currentOriginalImage = m_imageLoader->loadWithOpenCV(fileName);

    if (m_currentOriginalImage.empty()) {
        QMessageBox::warning(this, "Error", "Failed to load image with OpenCV.");
        return;
    }

    QImage originalQImage = m_imageLoader->matToQImage(m_currentOriginalImage);

    ui->originalImageLabel->setPixmap(
        QPixmap::fromImage(originalQImage).scaled(
            ui->originalImageLabel->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
            )
        );

    updateProcessedImage();
    updateOverlay(); //not sure
}

void MainWindow::updateProcessedImage()
{
    if (m_currentOriginalImage.empty()) {
        return;
    }

    int blurStrength = ui->blurSlider->value();
    bool normalizeEnabled = ui->normalizeCheckBox->isChecked();
    bool resizeEnabled = ui->resizeCheckBox->isChecked();
    double scale = ui->resizeSlider->value() / 100.0;
    bool grayscaleEnabled = ui->grayscaleCheckBox->isChecked();

    m_currentProcessedImage = m_preprocessingPipeline->preprocess(
        m_currentOriginalImage,
        blurStrength,
        normalizeEnabled,
        resizeEnabled,
        scale,
        grayscaleEnabled
        );

    if (m_currentProcessedImage.empty()) {
        return;
    }

    qDebug() << "Processed image:" << m_currentProcessedImage.cols
             << "x" << m_currentProcessedImage.rows;

    QImage processedQImage = m_imageLoader->matToQImage(m_currentProcessedImage);

    ui->processedImageLabel->setPixmap(
        QPixmap::fromImage(processedQImage).scaled(
            ui->processedImageLabel->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
            )
        );
}

void MainWindow::saveProcessedImage()
{
    if (m_currentProcessedImage.empty()) {
        QMessageBox::warning(this, "Error", "No processed image to save.");
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(
        this,
        "Save Processed Image",
        "",
        "PNG Image (*.png);;TIFF Image (*.tif *.tiff);;JPEG Image (*.jpg *.jpeg)"
        );

    if (fileName.isEmpty()) {
        return;
    }

    bool success = cv::imwrite(fileName.toStdString(), m_currentProcessedImage);

    if (!success) {
        QMessageBox::warning(this, "Error", "Failed to save processed image.");
        return;
    }

    QMessageBox::information(this, "Saved", "Processed image saved successfully.");
}

void MainWindow::loadMask()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "Load Mask",
        "",
        "Images (*.png *.tif *.tiff)"
        );

    if (fileName.isEmpty()) {
        return;
    }

    m_currentMask = m_imageLoader->loadWithOpenCV(fileName);

    if (m_currentMask.empty()) {
        QMessageBox::warning(this, "Error", "Failed to load mask.");
        return;
    }

    qDebug() << "Mask loaded:" << m_currentMask.cols << "x" << m_currentMask.rows
             << "channels:" << m_currentMask.channels();

    updateOverlay();
}

void MainWindow::updateOverlay()
{
    if (m_currentOriginalImage.empty() || m_currentMask.empty()) {
        return;
    }

    cv::Mat colorOriginal;

    if (m_currentOriginalImage.channels() == 1) {
        cv::cvtColor(m_currentOriginalImage, colorOriginal, cv::COLOR_GRAY2BGR);
    } else {
        colorOriginal = m_currentOriginalImage.clone();
    }

    cv::Mat maskGray;

    if (m_currentMask.channels() == 3) {
        cv::cvtColor(m_currentMask, maskGray, cv::COLOR_BGR2GRAY);
    } else {
        maskGray = m_currentMask.clone();
    }

    cv::Mat coloredMask;
    cv::applyColorMap(maskGray, coloredMask, cv::COLORMAP_JET);

    double alpha = 0.5;

    cv::Mat overlay;
    cv::addWeighted(colorOriginal, 1.0, coloredMask, alpha, 0, overlay);

    QImage overlayQImage = m_imageLoader->matToQImage(overlay);

    ui->processedImageLabel->setPixmap(
        QPixmap::fromImage(overlayQImage).scaled(
            ui->processedImageLabel->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
            )
        );
}

void MainWindow::batchPreprocessFolder()
{
    QString inputFolder = QFileDialog::getExistingDirectory(
        this,
        "Select Raw Image Folder"
        );

    if (inputFolder.isEmpty()) {
        return;
    }

    QString outputFolder = QFileDialog::getExistingDirectory(
        this,
        "Select Output Folder for Preprocessed Images"
        );

    if (outputFolder.isEmpty()) {
        return;
    }

    QDir inputDir(inputFolder);

    QStringList filters;
    filters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.tif" << "*.tiff";

    QFileInfoList files = inputDir.entryInfoList(
        filters,
        QDir::Files,
        QDir::Name
        );

    if (files.isEmpty()) {
        QMessageBox::warning(this, "No Images", "No supported image files found.");
        return;
    }

    int blurStrength = ui->blurSlider->value();
    bool normalizeEnabled = ui->normalizeCheckBox->isChecked();
    bool resizeEnabled = ui->resizeCheckBox->isChecked();
    double scale = ui->resizeSlider->value() / 100.0;
    bool grayscaleEnabled = ui->grayscaleCheckBox->isChecked();

    int successCount = 0;
    int failCount = 0;

    for (const QFileInfo &fileInfo : files) {
        QString inputPath = fileInfo.absoluteFilePath();

        cv::Mat image = m_imageLoader->loadWithOpenCV(inputPath);

        if (image.empty()) {
            failCount++;
            continue;
        }

        cv::Mat processed = m_preprocessingPipeline->preprocess(
            image,
            blurStrength,
            normalizeEnabled,
            resizeEnabled,
            scale,
            grayscaleEnabled
            );

        if (processed.empty()) {
            failCount++;
            continue;
        }

        QString baseName = fileInfo.completeBaseName();
        QString outputPath = outputFolder + "/" + baseName + "_processed.png";

        bool saved = cv::imwrite(outputPath.toStdString(), processed);

        if (saved) {
            successCount++;
        } else {
            failCount++;
        }
    }

    QMessageBox::information(
        this,
        "Batch Preprocessing Complete",
        QString("Processed: %1\nFailed: %2").arg(successCount).arg(failCount)
        );
}
