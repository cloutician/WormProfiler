#include "WormProfile.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <vector>
#include <opencv2/imgcodecs.hpp>

namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

cv::Mat readLabels(const QString &path, QString *hash, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { fail(error, file.errorString()); return {}; }
    const QByteArray bytes = file.readAll();
    const bool classic = bytes.startsWith(QByteArray::fromHex("49492a00")) || bytes.startsWith(QByteArray::fromHex("4d4d002a"));
    const bool bigTiff = bytes.startsWith(QByteArray::fromHex("49492b00")) || bytes.startsWith(QByteArray::fromHex("4d4d002b"));
    if (!classic && !bigTiff) { fail(error, "This file is not a TIFF image."); return {}; }
    try {
        const std::vector<uchar> buffer(bytes.begin(), bytes.end());
        std::vector<cv::Mat> pages;
        if (!cv::imdecodemulti(buffer, cv::IMREAD_UNCHANGED, pages, cv::Range(0, 2)) || pages.empty()) {
            fail(error, "The TIFF could not be decoded."); return {};
        }
        if (pages.size() != 1) {
            fail(error, "Multi-page TIFFs are not measured as a single image. Export one 2D Simple Segmentation TIFF per observation from ilastik."); return {};
        }
        const cv::Mat &labels = pages.front();
        if (labels.channels() != 1 || (labels.depth() != CV_8U && labels.depth() != CV_16U)) {
            fail(error, "Expected a single-channel 8-bit or 16-bit label image. In ilastik, export Simple Segmentation, not Probabilities or a colour rendering."); return {};
        }
        if (hash) *hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        return labels;
    } catch (const cv::Exception &exception) {
        fail(error, "TIFF decoding failed: " + QString::fromStdString(exception.what()));
        return {};
    }
}

QString csvText(QString value)
{
    // Protect spreadsheet users from formulas in editable IDs, notes, and filenames.
    const QString trimmed = value.trimmed();
    if (!trimmed.isEmpty() && QString("=+-@").contains(trimmed.front())) value.prepend('\'');
    value.replace('"', "\"\"");
    return '"' + value + '"';
}

bool validScale(double value) { return std::isfinite(value) && value >= 0 && value <= 1e6; }

QString pathIdentity(const QString &path)
{
    const QFileInfo info(path);
    QString identity = info.canonicalFilePath();
    if (identity.isEmpty()) identity = QDir::cleanPath(info.absoluteFilePath());
#ifdef Q_OS_WIN
    identity = identity.toCaseFolded();
#endif
    return identity;
}

QString metadataProblem(const WormProfile &profile)
{
    if (profile.wormId.trimmed().isEmpty()) return "Enter a worm ID.";
    QSet<QString> ids, paths;
    for (const auto &observation : profile.observations) {
        const QString path = pathIdentity(observation.imagePath);
        if (observation.id.isEmpty() || ids.contains(observation.id) || observation.imagePath.isEmpty() || paths.contains(path)
            || !observation.date.isValid() || !validScale(observation.pixelWidthUm) || !validScale(observation.pixelHeightUm)) {
            return "An observation has an invalid or duplicate ID/path, date, or pixel calibration.";
        }
        ids.insert(observation.id); paths.insert(path);
    }
    for (auto it = profile.classMapping.cbegin(); it != profile.classMapping.cend(); ++it) {
        if (it.key() < 0 || it.key() > 65535 || static_cast<int>(it.value()) < 0 || static_cast<int>(it.value()) > 4) return "Invalid tissue-class mapping.";
    }
    return {};
}
}

QString tissueClassName(TissueClass tissue)
{
    static const std::array names = {"Donor", "Host", "Background", "Eyes", "Ignore"};
    return QString::fromLatin1(names.at(static_cast<size_t>(tissue)));
}

QColor tissueClassColor(TissueClass tissue)
{
    static const std::array colors = {"#db6272", "#29a28f", "#e1e5e8", "#e1b340", "#6d7782"};
    return QColor(colors.at(static_cast<size_t>(tissue)));
}

void WormProfile::sortObservations()
{
    std::stable_sort(observations.begin(), observations.end(), [](const auto &first, const auto &second) {
        return first.date == second.date ? first.imagePath < second.imagePath : first.date < second.date;
    });
}

TissueAreas WormProfile::areas(const WormObservation &observation) const
{
    TissueAreas result;
    for (auto it = observation.statistics.labelPixels.cbegin(); it != observation.statistics.labelPixels.cend(); ++it) {
        const auto mapping = classMapping.constFind(it.key());
        if (mapping == classMapping.cend()) result.unmapped += it.value();
        else result.pixels.at(static_cast<size_t>(mapping.value())) += it.value();
    }
    return result;
}

bool WormProfile::save(const QString &path, QString *error) const
{
    const QString problem = metadataProblem(*this);
    if (!problem.isEmpty()) return fail(error, problem);
    for (const auto &observation : observations) if (pathIdentity(path) == pathIdentity(observation.imagePath)) return fail(error, "A profile cannot overwrite an input TIFF.");
    QJsonArray mapping, images;
    for (auto it = classMapping.cbegin(); it != classMapping.cend(); ++it) {
        mapping.append(QJsonObject{{"label", it.key()}, {"tissue", static_cast<int>(it.value())}});
    }
    const QDir directory(QFileInfo(path).absolutePath());
    for (const auto &observation : observations) {
        images.append(QJsonObject{{"id", observation.id}, {"imagePath", directory.relativeFilePath(observation.imagePath)},
                                  {"date", observation.date.toString(Qt::ISODate)}, {"notes", observation.notes},
                                  {"pixelWidthUm", observation.pixelWidthUm}, {"pixelHeightUm", observation.pixelHeightUm}});
    }
    const QJsonObject object{{"format", "WormProfiler worm profile"}, {"version", 1}, {"wormId", wormId},
                             {"notes", notes}, {"classMapping", mapping}, {"observations", images}};
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(object).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return fail(error, file.errorString());
    return true;
}

bool WormProfile::load(const QString &path, WormProfile *profile, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const auto object = document.object();
    if (!profile || parseError.error != QJsonParseError::NoError || !document.isObject()
        || object["format"].toString() != "WormProfiler worm profile" || object["version"].toDouble(-1) != 1
        || !object["wormId"].isString() || object["wormId"].toString().trimmed().isEmpty()
        || !object["notes"].isString() || !object["observations"].isArray() || !object["classMapping"].isArray()) {
        return fail(error, "This is not a supported WormProfiler worm profile.");
    }
    WormProfile parsed;
    parsed.wormId = object["wormId"].toString();
    parsed.notes = object["notes"].toString();
    for (const auto &value : object["classMapping"].toArray()) {
        const auto entry = value.toObject();
        const double label = entry["label"].toDouble(-1), tissue = entry["tissue"].toDouble(-1);
        if (!entry["label"].isDouble() || label < 0 || label > 65535 || label != std::floor(label)
            || !entry["tissue"].isDouble() || tissue < 0 || tissue > 4 || tissue != std::floor(tissue)
            || parsed.classMapping.contains(static_cast<int>(label))) return fail(error, "Invalid or duplicate tissue-class mapping.");
        parsed.classMapping.insert(static_cast<int>(label), static_cast<TissueClass>(static_cast<int>(tissue)));
    }
    QSet<QString> ids, paths;
    const QDir directory(QFileInfo(path).absolutePath());
    for (const auto &value : object["observations"].toArray()) {
        const auto entry = value.toObject();
        WormObservation observation;
        observation.id = entry["id"].toString();
        observation.imagePath = QDir::cleanPath(directory.absoluteFilePath(entry["imagePath"].toString()));
        observation.date = QDate::fromString(entry["date"].toString(), Qt::ISODate);
        observation.notes = entry["notes"].toString();
        observation.pixelWidthUm = entry["pixelWidthUm"].toDouble(-1);
        observation.pixelHeightUm = entry["pixelHeightUm"].toDouble(-1);
        const QString identity = pathIdentity(observation.imagePath);
        if (!entry["id"].isString() || observation.id.isEmpty() || ids.contains(observation.id)
            || !entry["imagePath"].isString() || entry["imagePath"].toString().isEmpty() || paths.contains(identity)
            || !entry["date"].isString() || !observation.date.isValid() || !entry["notes"].isString()
            || !entry["pixelWidthUm"].isDouble() || !entry["pixelHeightUm"].isDouble()
            || !validScale(observation.pixelWidthUm) || !validScale(observation.pixelHeightUm)) {
            return fail(error, "An observation has an invalid ID, path, date, or pixel calibration.");
        }
        ids.insert(observation.id); paths.insert(identity);
        parsed.observations.append(observation);
    }
    parsed.sortObservations();
    *profile = std::move(parsed);
    return true;
}

QString WormProfile::exportProblem() const
{
    const QString problem = metadataProblem(*this);
    if (!problem.isEmpty()) return problem;
    if (observations.isEmpty()) return "Add segmentation TIFFs to this profile.";
    for (const auto &observation : observations) {
        const QString name = QFileInfo(observation.imagePath).fileName();
        if (!observation.date.isValid()) return name + ": select an observation date.";
        if (!observation.statistics.valid()) return name + ": " + (observation.statistics.error.isEmpty() ? "not measured yet." : observation.statistics.error);
        if (areas(observation).unmapped) return name + ": assign every detected label to a tissue class or Ignore.";
        if (!validScale(observation.pixelWidthUm) || !validScale(observation.pixelHeightUm)
            || ((observation.pixelWidthUm > 0) != (observation.pixelHeightUm > 0))) {
            return name + ": enter both pixel dimensions, or set both to Unknown.";
        }
    }
    return {};
}

bool WormProfile::writeCsv(const QString &path, QString *error) const
{
    const QString problem = exportProblem();
    if (!problem.isEmpty()) return fail(error, problem);
    for (const auto &observation : observations) if (pathIdentity(path) == pathIdentity(observation.imagePath)) return fail(error, "CSV export cannot overwrite an input TIFF.");
    WormProfile sorted = *this;
    sorted.sortObservations();
    const QDate firstDate = sorted.observations.first().date;
    QStringList mapping;
    for (auto it = classMapping.cbegin(); it != classMapping.cend(); ++it) mapping << QString::number(it.key()) + "=" + tissueClassName(it.value());
    QString csv = "worm_id,observation_id,date,days_since_first_observation,image_file,image_path,sha256,width_px,height_px,donor_px,host_px,background_px,eyes_px,ignored_px,unmapped_px,worm_px,donor_fraction_of_tissue,pixel_width_um,pixel_height_um,donor_um2,host_um2,background_um2,eyes_um2,observation_notes,profile_notes,label_mapping\r\n";
    for (const auto &observation : sorted.observations) {
        const TissueAreas counts = areas(observation);
        const bool calibrated = observation.pixelWidthUm > 0 && observation.pixelHeightUm > 0;
        const double pixelArea = observation.pixelWidthUm * observation.pixelHeightUm;
        QStringList row{csvText(wormId), csvText(observation.id), observation.date.toString(Qt::ISODate),
                        QString::number(firstDate.daysTo(observation.date)), csvText(QFileInfo(observation.imagePath).fileName()),
                        csvText(observation.imagePath), observation.statistics.sha256,
                        QString::number(observation.statistics.size.width()), QString::number(observation.statistics.size.height())};
        for (quint64 count : counts.pixels) row << QString::number(count);
        row << QString::number(counts.unmapped) << QString::number(counts.wormPixels());
        const quint64 tissuePixels = counts.pixels[0] + counts.pixels[1];
        row << (tissuePixels ? QString::number(static_cast<double>(counts.pixels[0]) / tissuePixels, 'g', 15) : QString());
        row << (calibrated ? QString::number(observation.pixelWidthUm, 'g', 15) : QString())
            << (calibrated ? QString::number(observation.pixelHeightUm, 'g', 15) : QString());
        for (int index = 0; index < 4; ++index) row << (calibrated ? QString::number(counts.pixels[index] * pixelArea, 'g', 15) : QString());
        row << csvText(observation.notes) << csvText(notes) << csvText(mapping.join(';'));
        csv += row.join(',') + "\r\n";
    }
    QSaveFile file(path);
    const QByteArray bytes = QByteArray::fromHex("efbbbf") + csv.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return fail(error, file.errorString());
    return true;
}

LabelImageStatistics WormMeasurements::inspectTiff(const QString &path)
{
    LabelImageStatistics result;
    const cv::Mat labels = readLabels(path, &result.sha256, &result.error);
    if (labels.empty()) return result;
    result.size = QSize(labels.cols, labels.rows);
    result.bitDepth = static_cast<int>(labels.elemSize1()) * 8;
    std::vector<quint64> histogram(65536);
    for (int y = 0; y < labels.rows; ++y) {
        for (int x = 0; x < labels.cols; ++x) {
            ++histogram[labels.depth() == CV_8U ? labels.ptr<uchar>(y)[x] : labels.ptr<quint16>(y)[x]];
        }
    }
    for (int label = 0; label < 65536; ++label) if (histogram[label]) result.labelPixels.insert(label, histogram[label]);
    if (result.labelPixels.size() > 256) result.error = "More than 256 label values detected. Export a tissue-class Simple Segmentation, not an intensity or probability image.";
    return result;
}

QImage WormMeasurements::previewTiff(const QString &path, const QMap<int, TissueClass> &mapping,
                                    const QString &expectedHash, QString *error)
{
    QString hash;
    const cv::Mat labels = readLabels(path, &hash, error);
    if (labels.empty()) return {};
    if (hash != expectedHash) { fail(error, "The TIFF changed since it was measured. Reload measurements before exporting."); return {}; }
    QImage result(labels.cols, labels.rows, QImage::Format_RGB32);
    if (result.isNull()) { fail(error, "Not enough memory to display this segmentation."); return {}; }
    std::vector<QRgb> colors(labels.depth() == CV_8U ? 256 : 65536);
    for (int label = 0; label < static_cast<int>(colors.size()); ++label) {
        const auto found = mapping.constFind(label);
        colors[label] = found == mapping.cend() ? QColor::fromHsv((label * 67 + 10) % 360, 100, 195).rgb()
                                              : tissueClassColor(found.value()).rgb();
    }
    for (int y = 0; y < labels.rows; ++y) {
        auto *line = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < labels.cols; ++x) {
            const int label = labels.depth() == CV_8U ? labels.ptr<uchar>(y)[x] : labels.ptr<quint16>(y)[x];
            line[x] = colors[label];
        }
    }
    return result;
}

bool WormMeasurements::measureProfile(WormProfile *profile, const std::function<bool(int, int)> &progress)
{
    for (int index = 0; index < profile->observations.size(); ++index) {
        if (progress && !progress(index, profile->observations.size())) return false;
        profile->observations[index].statistics = inspectTiff(profile->observations[index].imagePath);
    }
    return !progress || progress(profile->observations.size(), profile->observations.size());
}
