#include "src/ui/mainwindow.h"
#include "src/ui/DesktopStyle.h"
#include "src/ui/StartupSplash.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    configureDesktopApplication(a);
    MainWindow w;
    w.prepareForDisplay();
    auto *splash = new StartupSplash(w.screen());
    splash->setAttribute(Qt::WA_DeleteOnClose);
    splash->show();
    splash->finishWhenReady(w);
    return a.exec();
}
