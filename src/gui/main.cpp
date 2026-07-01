#include "gui/MainWindow.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("OpenNord"));
    QCoreApplication::setApplicationName(QStringLiteral("OpenNord"));
    QCoreApplication::setApplicationVersion(QStringLiteral(OPENNORD_VERSION));
    opennord::MainWindow window;
    window.show();
    return application.exec();
}

