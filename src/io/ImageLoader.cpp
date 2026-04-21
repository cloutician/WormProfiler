#include "ImageLoader.h"

cv::Mat ImageLoader::loadWithOpenCV(const QString &filePath) const
{
    return cv::imread(filePath.toStdString(), cv::IMREAD_UNCHANGED);
}

QImage ImageLoader::matToQImage(const cv::Mat &mat) const
{
    if (mat.empty()) {
        return QImage();
    }

    switch (mat.type()) {
    case CV_8UC1: {
        QImage image(mat.data,
                     mat.cols,
                     mat.rows,
                     static_cast<int>(mat.step),
                     QImage::Format_Grayscale8);
        return image.copy();
    }

    case CV_8UC3: {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        QImage image(rgb.data,
                     rgb.cols,
                     rgb.rows,
                     static_cast<int>(rgb.step),
                     QImage::Format_RGB888);
        return image.copy();
    }

    case CV_8UC4: {
        QImage image(mat.data,
                     mat.cols,
                     mat.rows,
                     static_cast<int>(mat.step),
                     QImage::Format_ARGB32);
        return image.copy();
    }

    default:
        return QImage();
    }
}