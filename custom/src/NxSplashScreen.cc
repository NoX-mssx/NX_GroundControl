#include "NxSplashScreen.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainter>
#include <QtGui/QScreen>

NxSplashScreen::NxSplashScreen()
    : _image(QStringLiteral(":/res/nx/Splash.png"))
{
    setFlags(Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setTitle(QCoreApplication::applicationName());
}

void NxSplashScreen::showFor(int milliseconds)
{
    NxSplashScreen *const splash = new NxSplashScreen();
    if (splash->_image.isNull()) {
        delete splash;
        return;
    }

    // Half the screen width (at most 1000 points), in the banner's 3:1 shape, centred
    const QScreen *const screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
    const int width = qMin(1000, available.width() / 2);
    const int height = width * splash->_image.height() / splash->_image.width();
    splash->setGeometry(available.x() + ((available.width() - width) / 2), available.y() + ((available.height() - height) / 2),
                        width, height);
    splash->show();
    splash->raise();

    // The app is busy loading right after this, so the banner is painted now
    QElapsedTimer waited;
    waited.start();
    while (!splash->_painted && (waited.elapsed() < 500)) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
    }

    QTimer::singleShot(milliseconds, splash, &NxSplashScreen::_dismiss);
}

void NxSplashScreen::_dismiss()
{
    // Hidden rather than closed: closing the only visible window would end the app if the main window were
    // not shown yet
    hide();
    deleteLater();
}

void NxSplashScreen::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.fillRect(QRect(QPoint(0, 0), size()), QColor(0x26, 0x2D, 0x33));
    painter.drawImage(QRect(QPoint(0, 0), size()), _image);
    _painted = true;
}

void NxSplashScreen::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    _dismiss();
}
