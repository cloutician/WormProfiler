#ifndef IMAGELOADER_H
#define IMAGELOADER_H

#include <QString>
#include <QImage>
#include <opencv2/opencv.hpp>

class ImageLoader
{
public:
    ImageLoader() = default;

    cv::Mat loadWithOpenCV(const QString &filePath) const;
    QImage matToQImage(const cv::Mat &mat) const;
};

#endif // IMAGELOADER_H