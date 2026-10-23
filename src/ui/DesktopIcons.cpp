#include "DesktopIcons.h"

#include <QFile>
#include <QIconEngine>
#include <QPainter>
#include <QPixmapCache>
#include <QSvgRenderer>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
class VectorIconEngine final : public QIconEngine
{
public:
    VectorIconEngine(QByteArray svg, QString cacheId, bool onAccent)
        : m_svg(std::move(svg)), m_cacheId(std::move(cacheId)), m_onAccent(onAccent) {}

    QIconEngine *clone() const override { return new VectorIconEngine(*this); }
    bool isNull() override { return m_svg.isEmpty(); }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const QTransform transform = painter->deviceTransform();
        const qreal scale = std::max(std::hypot(transform.m11(), transform.m12()),
                                     std::hypot(transform.m21(), transform.m22()));
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, scale));
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        if (size.isEmpty() || scale <= 0) return {};
        const QString key = QString("WormProfiler/icons/%1/%2/%3/%4/%5/%6/%7")
            .arg(m_cacheId).arg(m_onAccent).arg(size.width()).arg(size.height())
            .arg(mode).arg(state).arg(scale, 0, 'g', 12);
        QPixmap result;
        if (QPixmapCache::find(key, &result)) return result;
        result = QPixmap(size * scale);
        result.setDevicePixelRatio(scale);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        const auto color = mode == QIcon::Disabled ? (m_onAccent ? "#e7eeed" : "#a2a7ac")
                         : m_onAccent ? "#ffffff" : mode == QIcon::Selected ? "#146c60" : "#41484e";
        QByteArray svg = m_svg;
        svg.replace("currentColor", color);
        QSvgRenderer renderer(svg);
        renderer.render(&painter, QRect(QPoint(), size));
        painter.end();
        QPixmapCache::insert(key, result);
        return result;
    }

private:
    QByteArray m_svg;
    QString m_cacheId;
    bool m_onAccent;
};
}

QIcon DesktopIcons::icon(Symbol symbol, bool onAccent)
{
    static constexpr std::array names = {
        "image.svg", "folder-open.svg", "save.svg", "download.svg", "images.svg", "chevron-left.svg", "chevron-right.svg",
        "chevron-up.svg", "chevron-down.svg", "maximize.svg", "scan.svg", "sliders-horizontal.svg", "rotate-ccw.svg",
        "layers-2.svg", "folder-plus.svg", "flag.svg", "check.svg", "x.svg", "info.svg", "triangle-alert.svg", "wormprofiler.png",
        "search.svg", "eraser.svg", "copy.svg", "log-out.svg", "crop.svg", "circle-check.svg", "circle-x.svg", "ruler.svg", "calendar-days.svg",
        "undo-2.svg", "redo-2.svg"
    };
    const QString path = QString(":/icons/%1").arg(names.at(static_cast<size_t>(symbol)));
    if (symbol == Symbol::Application) return QIcon(path);
    QFile asset(path);
    if (!asset.open(QIODevice::ReadOnly)) return {};
    return QIcon(new VectorIconEngine(asset.readAll(), path, onAccent));
}
