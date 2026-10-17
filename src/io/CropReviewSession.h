#ifndef CROPREVIEWSESSION_H
#define CROPREVIEWSESSION_H

#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <functional>

enum class CropReviewState { Pending, Approved, Rejected, NeedsReview };

QString cropReviewStateName(CropReviewState state);

struct CropPreviewItem
{
    QString imagePath;
    QSize imageSize;
    QRect cropRect;
    QRect originalCropRect;
    bool predictionFound = false;
    QString statusText;
    CropReviewState reviewState = CropReviewState::Pending;
};

struct CropExportResult
{
    int exported = 0;
    int failed = 0;
    bool canceled = false;
    QString manifestPath;
    QStringList errors;
};

struct CropReviewSession
{
    QList<CropPreviewItem> items;
    QString modelPath;
    int cropMarginPixels = 0;
    double predictionThreshold = 0.65;
    int currentIndex = 0;

    bool save(const QString &path, QString *error) const;
    static bool load(const QString &path, CropReviewSession *session, QString *error);
    CropExportResult exportApproved(const QString &directory,
                                   const std::function<bool(int, int)> &continueExport = {}) const;
};

#endif
