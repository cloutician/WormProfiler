#pragma once

#include <QDate>
#include <QColor>
#include <QImage>
#include <QList>
#include <QMap>
#include <QSize>
#include <QString>
#include <array>
#include <functional>

enum class TissueClass { Donor, Host, Background, Eyes, Ignore };
QString tissueClassName(TissueClass tissue);
QColor tissueClassColor(TissueClass tissue);

struct LabelImageStatistics
{
    QSize size;
    int bitDepth = 0;
    QMap<int, quint64> labelPixels;
    QString sha256;
    QString error;
    bool valid() const { return error.isEmpty() && !size.isEmpty(); }
};

struct WormObservation
{
    QString id;
    QString imagePath;
    QDate date;
    QString notes;
    double pixelWidthUm = 0;
    double pixelHeightUm = 0;
    LabelImageStatistics statistics;
};

struct TissueAreas
{
    std::array<quint64, 5> pixels{};
    quint64 unmapped = 0;
    quint64 wormPixels() const { return pixels[0] + pixels[1] + pixels[3]; }
};

struct WormProfile
{
    QString wormId;
    QString notes;
    QMap<int, TissueClass> classMapping;
    QList<WormObservation> observations;

    bool save(const QString &path, QString *error) const;
    static bool load(const QString &path, WormProfile *profile, QString *error);
    void sortObservations();
    TissueAreas areas(const WormObservation &observation) const;
    QString exportProblem() const;
    bool writeCsv(const QString &path, QString *error) const;
};

namespace WormMeasurements {
LabelImageStatistics inspectTiff(const QString &path);
QImage previewTiff(const QString &path, const QMap<int, TissueClass> &mapping,
                   const QString &expectedHash, QString *error);
bool measureProfile(WormProfile *profile, const std::function<bool(int, int)> &progress = {});
}
