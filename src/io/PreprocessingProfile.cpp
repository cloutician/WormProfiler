#include "PreprocessingProfile.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

QJsonObject PreprocessingProfile::toJson() const
{
    return {{"format", "WormProfiler preprocessing profile"},
            {"version", 1},
            {"blurStrength", blurStrength},
            {"normalizeEnabled", normalizeEnabled},
            {"resizeEnabled", resizeEnabled},
            {"resizePercent", resizePercent},
            {"grayscaleEnabled", grayscaleEnabled},
            {"claheEnabled", claheEnabled},
            {"autoCropEnabled", autoCropEnabled},
            {"cropMarginPixels", cropMarginPixels}};
}

bool PreprocessingProfile::fromJson(const QJsonObject &object, PreprocessingProfile *profile, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (!profile || object.value("format").toString() != "WormProfiler preprocessing profile"
        || object.value("version").toDouble(-1) != 1) {
        return fail("This is not a supported WormProfiler JSON preprocessing profile.");
    }
    const auto validInteger = [&object](const char *key, int minimum, int maximum) {
        const QJsonValue value = object.value(key);
        const double number = value.toDouble(-1);
        return value.isDouble() && number >= minimum && number <= maximum
               && number == static_cast<int>(number);
    };
    if (!validInteger("blurStrength", 0, 10) || !validInteger("resizePercent", 10, 100)
        || !validInteger("cropMarginPixels", 0, 250)) {
        return fail("The profile contains an invalid blur, resize or crop margin value.");
    }
    for (const char *key : {"normalizeEnabled", "resizeEnabled", "grayscaleEnabled",
                            "claheEnabled", "autoCropEnabled"}) {
        if (!object.value(key).isBool()) {
            return fail(QString("The profile setting '%1' must be true or false.").arg(key));
        }
    }
    PreprocessingProfile parsed;
    parsed.blurStrength = object.value("blurStrength").toInt();
    parsed.normalizeEnabled = object.value("normalizeEnabled").toBool();
    parsed.resizeEnabled = object.value("resizeEnabled").toBool();
    parsed.resizePercent = object.value("resizePercent").toInt();
    parsed.grayscaleEnabled = object.value("grayscaleEnabled").toBool();
    parsed.claheEnabled = object.value("claheEnabled").toBool();
    parsed.autoCropEnabled = object.value("autoCropEnabled").toBool();
    parsed.cropMarginPixels = object.value("cropMarginPixels").toInt();
    *profile = parsed;
    return true;
}

bool PreprocessingProfile::save(const QString &path, QString *error) const
{
    QSaveFile file(path);
    const QByteArray data = QJsonDocument(toJson()).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool PreprocessingProfile::load(const QString &path, PreprocessingProfile *profile, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = "Choose a WormProfiler JSON profile. Legacy text profiles cannot be imported.";
        return false;
    }
    return fromJson(document.object(), profile, error);
}
