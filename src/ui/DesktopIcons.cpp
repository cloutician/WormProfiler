#include "DesktopIcons.h"

#include <QFile>
#include <QIconEngine>
#include <QPainter>
#include <QSvgRenderer>
#include <array>

namespace {
class VectorIconEngine final : public QIconEngine
{
public:
    VectorIconEngine(QByteArray svg, bool onAccent) : m_svg(std::move(svg)), m_onAccent(onAccent) {}

    QIconEngine *clone() const override { return new VectorIconEngine(*this); }
    bool isNull() override { return m_svg.isEmpty(); }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        const auto color = mode == QIcon::Disabled ? (m_onAccent ? "#e7eeed" : "#a2a7ac")
                         : m_onAccent ? "#ffffff" : mode == QIcon::Selected ? "#146c60" : "#41484e";
        QByteArray svg = m_svg;
        svg.replace("currentColor", color);
        QSvgRenderer renderer(svg);
        painter->save();
        renderer.render(painter, rect);
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap result(size * scale);
        result.setDevicePixelRatio(scale);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }

private:
    QByteArray m_svg;
    bool m_onAccent;
};
}

QIcon DesktopIcons::icon(Symbol symbol, bool onAccent)
{
    static constexpr std::array names = {
        "image", "folder-open", "save", "download", "images", "chevron-left", "chevron-right",
        "chevron-up", "chevron-down", "maximize", "scan", "sliders-horizontal", "rotate-ccw",
        "layers-2", "folder-plus", "flag", "check", "x", "info", "triangle-alert", "microscope",
        "search", "eraser", "copy", "log-out", "crop", "circle-check", "circle-x", "ruler", "calendar-days"
    };
    QFile asset(QString(":/icons/%1.svg").arg(names.at(static_cast<size_t>(symbol))));
    if (!asset.open(QIODevice::ReadOnly)) return {};
    return QIcon(new VectorIconEngine(asset.readAll(), onAccent));
}
