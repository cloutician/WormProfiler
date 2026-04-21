#include "mainwindow.h"
#include "ImageLoader.h"
#include "./ui_mainwindow.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QPixmap>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_imageLoader(new ImageLoader)
{
    ui->setupUi(this);

    connect(ui->actionOpen_Image, &QAction::triggered, this, &MainWindow::openImage);
}

MainWindow::~MainWindow()
{
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

    cv::Mat image = m_imageLoader->loadWithOpenCV(fileName);

    if (image.empty()) {
        QMessageBox::warning(this, "Error", "Failed to load image with OpenCV.");
        return;
    }

    qDebug() << "Loaded image:" << image.cols << "x" << image.rows
             << "channels:" << image.channels();

    QImage qimg = m_imageLoader->matToQImage(image);

    if (qimg.isNull()) {
        QMessageBox::warning(this, "Error", "Failed to convert image for display.");
        return;
    }

    ui->imageLabel->setPixmap(
        QPixmap::fromImage(qimg).scaled(
            ui->imageLabel->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
            )
        );
}