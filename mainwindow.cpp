#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <opencv2/opencv.hpp>
#include <Qdebug>

#include <QFileDialog>
#include <QmessageBox>
#include <QPixmap>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    cv::Mat testImage(100, 100, CV_8UC1, cv::Scalar(128));
    qDebug() << "OpenCV test:" << testImage.cols << "x" << testImage.rows;

    connect(ui->actionOpen_Image, &QAction::triggered, this, &MainWindow::openImage);
}

MainWindow::~MainWindow()
{
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

    QPixmap pixmap(fileName);

    if (pixmap.isNull()) {
        QMessageBox::warning(this, "Error", "Failed to load image.");
        return;
    }

    ui->imageLabel->setPixmap(
        pixmap.scaled(
            ui->imageLabel->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
            )
        );
}