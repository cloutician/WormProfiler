#ifndef PROCESSINGJOBS_H
#define PROCESSINGJOBS_H

#include "CropReviewSession.h"
#include "PreprocessingProfile.h"
#include <QStringList>
#include <functional>

struct ProcessingJobResult
{
    CropReviewSession session;
    int succeeded = 0;
    int failed = 0;
    bool canceled = false;
    QString fatalError;
    QStringList errors;
    QString outputFolder;
};

namespace ProcessingJobs {
using Progress = std::function<bool(int, int)>;
QStringList imageFiles(const QString &folder);
ProcessingJobResult createReview(const QStringList &files, const QString &modelPath, int margin,
                                 double threshold, const Progress &progress);
ProcessingJobResult preprocess(const QStringList &files, const QString &outputFolder,
                               const PreprocessingProfile &profile, const Progress &progress);
}

#endif
