#include "CropReviewSession.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <opencv2/imgcodecs.hpp>

#include <limits>
#include <vector>

namespace {
QJsonObject rectangleJson(const QRect &rect)
{
    return {{"x", rect.x()}, {"y", rect.y()}, {"width", rect.width()}, {"height", rect.height()}};
}

bool readInteger(const QJsonObject &object, const char *key, int minimum, int maximum, int *result)
{
    const QJsonValue value = object.value(key);
    const double number = value.toDouble(-1);
    if (!value.isDouble() || number < minimum || number > maximum || number != static_cast<int>(number)) {
        return false;
    }
    *result = static_cast<int>(number);
    return true;
}

bool readRectangle(const QJsonValue &value, const QSize &imageSize, QRect *rect)
{
    if (!value.isObject()) return false;
    const QJsonObject object = value.toObject();
    int x, y, width, height;
    if (!readInteger(object, "x", 0, imageSize.width() - 1, &x)
        || !readInteger(object, "y", 0, imageSize.height() - 1, &y)
        || !readInteger(object, "width", 1, imageSize.width() - x, &width)
        || !readInteger(object, "height", 1, imageSize.height() - y, &height)) return false;
    *rect = QRect(x, y, width, height);
    return true;
}

QString unusedPath(const QDir &directory, const QString &base, const QString &extension)
{
    QString path = directory.filePath(base + extension);
    for (int suffix = 2; QFileInfo::exists(path); ++suffix) {
        path = directory.filePath(QString("%1_%2%3").arg(base).arg(suffix).arg(extension));
    }
    return path;
}

QString csvValue(QString value)
{
    // Prevent spreadsheet applications from interpreting source filenames as formulas.
    if (!value.isEmpty() && QString("=+-@\t\r").contains(value.front())) value.prepend('\'');
    value.replace('"', "\"\"");
    return '"' + value + '"';
}

bool writeAtomic(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}
}

QString cropReviewStateName(CropReviewState state)
{
    switch (state) {
    case CropReviewState::Approved: return "Approved";
    case CropReviewState::Rejected: return "Rejected";
    case CropReviewState::NeedsReview: return "Needs review";
    default: return "Pending";
    }
}

bool CropReviewSession::save(const QString &path, QString *error) const
{
    const QDir baseDirectory = QFileInfo(path).absoluteDir();
    QJsonArray images;
    for (const CropPreviewItem &item : items) {
        images.append(QJsonObject{{"imagePath", baseDirectory.relativeFilePath(item.imagePath)},
                                  {"width", item.imageSize.width()}, {"height", item.imageSize.height()},
                                  {"crop", rectangleJson(item.cropRect)},
                                  {"suggestedCrop", rectangleJson(item.originalCropRect)},
                                  {"predictionFound", item.predictionFound},
                                  {"predictionMessage", item.statusText},
                                  {"reviewState", static_cast<int>(item.reviewState)}});
    }
    const QJsonObject object{{"format", "WormProfiler crop review"}, {"version", 1},
                             {"savedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                             {"modelPath", modelPath.isEmpty() ? QString() : baseDirectory.relativeFilePath(modelPath)},
                             {"cropMarginPixels", cropMarginPixels}, {"predictionThreshold", predictionThreshold},
                             {"currentIndex", currentIndex},
                             {"images", images}};
    return writeAtomic(path, QJsonDocument(object).toJson(), error);
}

bool CropReviewSession::load(const QString &path, CropReviewSession *session, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (!session) return fail("No session destination was supplied.");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject object = document.object();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || object.value("format").toString() != "WormProfiler crop review" || object.value("version").toDouble(-1) != 1
        || !object.value("images").isArray() || object.value("images").toArray().isEmpty()) {
        return fail("This is not a supported WormProfiler crop review session.");
    }
    CropReviewSession parsed;
    const QDir baseDirectory = QFileInfo(path).absoluteDir();
    const QString storedModelPath = object.value("modelPath").toString();
    parsed.modelPath = storedModelPath.isEmpty() ? QString() : QDir::cleanPath(baseDirectory.absoluteFilePath(storedModelPath));
    if (!readInteger(object, "cropMarginPixels", 0, 100000, &parsed.cropMarginPixels)) {
        return fail("The session has an invalid crop margin.");
    }
    const QJsonValue threshold = object.value("predictionThreshold");
    if (!threshold.isDouble() || threshold.toDouble() < 0 || threshold.toDouble() > 1) {
        return fail("The session has an invalid prediction threshold.");
    }
    parsed.predictionThreshold = threshold.toDouble();
    for (const QJsonValue &value : object.value("images").toArray()) {
        const QJsonObject image = value.toObject();
        CropPreviewItem item;
        int width, height, state;
        if (!value.isObject() || !image.value("imagePath").isString() || image.value("imagePath").toString().isEmpty()
            || !readInteger(image, "width", 1, std::numeric_limits<int>::max(), &width)
            || !readInteger(image, "height", 1, std::numeric_limits<int>::max(), &height)
            || !readInteger(image, "reviewState", 0, 3, &state) || !image.value("predictionFound").isBool()) {
            return fail(QString("Invalid image entry %1 in the review session.").arg(parsed.items.size() + 1));
        }
        item.imageSize = QSize(width, height);
        if (!readRectangle(image.value("crop"), item.imageSize, &item.cropRect)
            || !readRectangle(image.value("suggestedCrop"), item.imageSize, &item.originalCropRect)) {
            return fail(QString("Invalid crop rectangle for image entry %1.").arg(parsed.items.size() + 1));
        }
        item.imagePath = QDir::cleanPath(baseDirectory.absoluteFilePath(image.value("imagePath").toString()));
        item.predictionFound = image.value("predictionFound").toBool();
        item.statusText = image.value("predictionMessage").toString();
        item.reviewState = static_cast<CropReviewState>(state);
        parsed.items.append(item);
    }
    if (!readInteger(object, "currentIndex", 0, parsed.items.size() - 1, &parsed.currentIndex)) {
        return fail("The session has an invalid current image index.");
    }
    *session = parsed;
    return true;
}

CropExportResult CropReviewSession::exportApproved(const QString &directory,
                                                  const std::function<bool(int, int)> &continueExport) const
{
    CropExportResult result;
    const QDir destination(directory);
    if (!destination.exists()) {
        result.errors.append("The export folder does not exist.");
        return result;
    }
    QString csv = "source_image,output_image,review_state,export_result,x,y,width,height,source_width,source_height,"
                  "prediction_found,prediction_message,model_path,crop_margin_px,prediction_threshold,exported_at_utc\r\n";
    const QString exportedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    for (int index = 0; index < items.size(); ++index) {
        const CropPreviewItem &item = items[index];
        QString outcome = "not_approved";
        QString outputPath;
        if (!result.canceled && continueExport && !continueExport(index, static_cast<int>(items.size()))) {
            result.canceled = true;
        }
        if (item.reviewState == CropReviewState::Approved) {
            outcome = "canceled";
            if (!result.canceled) {
                QString failure;
                try {
                    QFile input(item.imagePath);
                    if (!input.open(QIODevice::ReadOnly)) {
                        failure = input.errorString();
                    } else {
                        const QByteArray bytes = input.readAll();
                        const std::vector<uchar> buffer(bytes.begin(), bytes.end());
                        const cv::Mat image = cv::imdecode(buffer, cv::IMREAD_UNCHANGED);
                        if (image.empty()) {
                            failure = "The source image could not be decoded.";
                        } else if ((image.depth() != CV_8U && image.depth() != CV_16U)
                                   || (image.channels() != 1 && image.channels() != 3 && image.channels() != 4)) {
                            failure = "PNG export requires unsigned 8-bit or 16-bit images with 1, 3 or 4 channels. "
                                      "This source was not converted or exported.";
                        } else if (QSize(image.cols, image.rows) != item.imageSize) {
                            failure = "Source image dimensions changed since review. Review this image again.";
                        } else if (!QRect(QPoint(0, 0), item.imageSize).contains(item.cropRect) || item.cropRect.isEmpty()) {
                            failure = "The approved crop is outside the source image.";
                        } else {
                            const QRect rect = item.cropRect;
                            const cv::Mat crop = image(cv::Rect(rect.x(), rect.y(), rect.width(), rect.height()));
                            std::vector<uchar> encoded;
                            if (!cv::imencode(".png", crop, encoded)) {
                                failure = "The crop could not be encoded as PNG.";
                            } else {
                                outputPath = unusedPath(destination, QFileInfo(item.imagePath).completeBaseName() + "_crop", ".png");
                                const QByteArray data(reinterpret_cast<const char *>(encoded.data()), static_cast<qsizetype>(encoded.size()));
                                if (!writeAtomic(outputPath, data, &failure)) outputPath.clear();
                            }
                        }
                    }
                } catch (const cv::Exception &exception) {
                    failure = QString::fromStdString(exception.what());
                }
                if (failure.isEmpty()) {
                    ++result.exported;
                    outcome = "exported";
                } else {
                    ++result.failed;
                    outcome = "failed";
                    result.errors.append(QFileInfo(item.imagePath).fileName() + ": " + failure);
                }
            }
        }
        const QStringList fields = {item.imagePath, outputPath, cropReviewStateName(item.reviewState), outcome,
                                    QString::number(item.cropRect.x()), QString::number(item.cropRect.y()),
                                    QString::number(item.cropRect.width()), QString::number(item.cropRect.height()),
                                    QString::number(item.imageSize.width()), QString::number(item.imageSize.height()),
                                    item.predictionFound ? "true" : "false", item.statusText, modelPath,
                                    QString::number(cropMarginPixels), QString::number(predictionThreshold), exportedAt};
        QStringList escaped;
        for (const QString &field : fields) escaped.append(csvValue(field));
        csv += escaped.join(',') + "\r\n";
    }
    const QString manifestPath = unusedPath(destination, "crop_manifest", ".csv");
    QString error;
    if (writeAtomic(manifestPath, csv.toUtf8(), &error)) result.manifestPath = manifestPath;
    else result.errors.append("Could not save the CSV manifest: " + error);
    return result;
}
