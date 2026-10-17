#ifndef PREPROCESSINGPROFILE_H
#define PREPROCESSINGPROFILE_H

#include <QJsonObject>
#include <QString>

struct PreprocessingProfile
{
    int blurStrength = 2;
    bool normalizeEnabled = true;
    bool resizeEnabled = true;
    int resizePercent = 50;
    bool grayscaleEnabled = true;
    bool claheEnabled = false;
    bool autoCropEnabled = false;
    int cropMarginPixels = 40;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, PreprocessingProfile *profile, QString *error);
    bool save(const QString &path, QString *error) const;
    static bool load(const QString &path, PreprocessingProfile *profile, QString *error);
};

#endif
