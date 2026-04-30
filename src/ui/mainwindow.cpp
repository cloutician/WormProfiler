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
}

void MainWindow::updateProcessedImage()
{
    if (m_currentOriginalImage.empty()) {
        return;
    }

    int blurStrength = ui->blurSlider->value();
    bool normalizeEnabled = ui->normalizeCheckBox->isChecked();

    m_currentProcessedImage = m_preprocessingPipeline->preprocess(
        m_currentOriginalImage,
        blurStrength,
        normalizeEnabled
        );

    if (m_currentProcessedImage.empty()) {
        return;
    }

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