#include "src/ui/mainwindow.h"
#include "src/ui/DesktopStyle.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    configureDesktopApplication(a);
    MainWindow w;
    w.show();
    return a.exec();
}
