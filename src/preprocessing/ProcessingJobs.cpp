#include "ProcessingJobs.h"
#include "ImageLoader.h"
#include "PreprocessingPipeline.h"
#include "WormCropper.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <opencv2/imgcodecs.hpp>
#include <stdexcept>
#include <vector>

namespace {
QString uniquePath(const QDir &folder, const QString &base, const QString &suffix)
{
    QString path = folder.filePath(base + suffix);
    for (int number = 2; QFileInfo::exists(path); ++number) {
        path = folder.filePath(QString("%1_%2%3").arg(base).arg(number).arg(suffix));
    }
    return path;
}
}

QStringList ProcessingJobs::imageFiles(const QString &folder)
{
    QStringList paths;
    const QStringList suffixes = {"png", "jpg", "jpeg", "bmp", "tif", "tiff"};
    const auto entries = QDir(folder).entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (suffixes.contains(entry.suffix().toLower())) paths.append(entry.absoluteFilePath());
    }
    return paths;
}

ProcessingJobResult ProcessingJobs::createReview(const QStringList &files, const QString &modelPath, int margin,
                                                  double threshold, const Progress &progress)
{
    ProcessingJobResult result;
    WormCropper cropper;
    std::string error;
    if (!cropper.loadModel(modelPath.toStdString(), &error)) {
        result.fatalError = QString::fromStdString(error);
        return result;
    }
    result.session.modelPath = modelPath;
    result.session.cropMarginPixels = margin;
    result.session.predictionThreshold = threshold;
    ImageLoader loader;
    for (int index = 0; index < files.size(); ++index) {
        if (progress && !progress(index, static_cast<int>(files.size()))) { result.canceled = true; break; }
        try {
            const cv::Mat image = loader.loadWithOpenCV(files[index]);
            if (image.empty()) throw std::runtime_error("Image could not be decoded.");
            const auto prediction = cropper.predictCrop(image, margin, static_cast<float>(threshold));
            CropPreviewItem item;
            item.imagePath = files[index];
            item.imageSize = QSize(image.cols, image.rows);
            item.cropRect = QRect(prediction.cropRect.x, prediction.cropRect.y,
                                  prediction.cropRect.width, prediction.cropRect.height);
            if (item.cropRect.isEmpty()) item.cropRect = QRect(0, 0, image.cols, image.rows);
            item.originalCropRect = item.cropRect;
            item.predictionFound = prediction.found;
            item.statusText = QString::fromStdString(prediction.message);
            item.reviewState = prediction.found ? CropReviewState::Pending : CropReviewState::NeedsReview;
            result.session.items.append(item);
            ++result.succeeded;
        } catch (const std::exception &exception) {
            ++result.failed;
            result.errors.append(QFileInfo(files[index]).fileName() + ": " + QString::fromUtf8(exception.what()));
        }
    }
    return result;
}

ProcessingJobResult ProcessingJobs::preprocess(const QStringList &files, const QString &outputFolder,
                                               const PreprocessingProfile &profile, const Progress &progress)
{
    ProcessingJobResult result;
    result.outputFolder = outputFolder;
    if (!QDir().mkpath(outputFolder)) { result.fatalError = "The output folder could not be created."; return result; }
    const QDir destination(outputFolder);
    QString profileError;
    if (!profile.save(uniquePath(destination, "preprocessing-profile", ".json"), &profileError)) {
        result.fatalError = "The processing profile could not be saved: " + profileError;
        return result;
    }
    ImageLoader loader;
    PreprocessingPipeline pipeline;
    for (int index = 0; index < files.size(); ++index) {
        if (progress && !progress(index, static_cast<int>(files.size()))) { result.canceled = true; break; }
        try {
            const cv::Mat image = loader.loadWithOpenCV(files[index]);
            if (image.empty()) throw std::runtime_error("Image could not be decoded.");
            const cv::Mat processed = pipeline.preprocess(image, profile.blurStrength, profile.normalizeEnabled,
                profile.resizeEnabled, profile.resizePercent / 100.0, profile.grayscaleEnabled,
                profile.claheEnabled, profile.autoCropEnabled, profile.cropMarginPixels);
            if (processed.empty()) throw std::runtime_error("Processing returned an empty image.");
            if (processed.depth() != CV_8U && processed.depth() != CV_16U) {
                throw std::runtime_error("PNG export requires unsigned 8-bit or 16-bit pixels.");
            }
            std::vector<uchar> bytes;
            if (!cv::imencode(".png", processed, bytes)) throw std::runtime_error("PNG encoding failed.");
            QSaveFile file(uniquePath(destination, QFileInfo(files[index]).completeBaseName() + "_processed", ".png"));
            if (!file.open(QIODevice::WriteOnly)
                || file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<qint64>(bytes.size())) != bytes.size()
                || !file.commit()) throw std::runtime_error(file.errorString().toStdString());
            ++result.succeeded;
        } catch (const std::exception &exception) {
            ++result.failed;
            result.errors.append(QFileInfo(files[index]).fileName() + ": " + QString::fromUtf8(exception.what()));
        }
    }
    return result;
}
