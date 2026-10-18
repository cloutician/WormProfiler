#pragma once

#include <QElapsedTimer>
#include <QPixmap>
#include <QPointer>
#include <QWidget>

class QLabel;
class QMouseEvent;
class QPaintEvent;
class QScreen;
class QShowEvent;

class StartupSplash final : public QWidget
{
public:
    static constexpr int MinimumDisplayMs = 1800;

    explicit StartupSplash(QScreen *screen = nullptr);
    void finishWhenReady(QWidget &window);

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void cacheArtwork();

    QPixmap m_background;
    QLabel *m_logo = nullptr;
    QLabel *m_status = nullptr;
    QPointer<QWidget> m_target;
    QElapsedTimer m_displayTime;
    bool m_handoffQueued = false;
};
