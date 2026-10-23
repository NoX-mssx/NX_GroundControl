#pragma once

#include <QtGui/QImage>
#include <QtGui/QRasterWindow>

/// NX Rover banner shown while the app starts. It stays on top of the main window for a few seconds and
/// closes by itself; a click closes it earlier. Plain QtGui, since the app has no widgets.
class NxSplashScreen : public QRasterWindow
{
    Q_OBJECT

public:
    /// Shows the banner at once and closes it after @p milliseconds of running event loop
    static void showFor(int milliseconds);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    NxSplashScreen();
    void _dismiss();

    QImage _image;
    bool _painted = false;
};
