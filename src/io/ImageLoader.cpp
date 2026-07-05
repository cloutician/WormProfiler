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
