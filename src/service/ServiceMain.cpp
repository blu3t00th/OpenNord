#include "service/NamedPipeServer.h"
#include "service/VpnController.h"
#include "service/WindowsService.h"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("OpenNordService"));
    const auto arguments = application.arguments();
    if (arguments.contains(QStringLiteral("--install"))) {
        const auto error = opennord::WindowsService::install();
        if (!error.isEmpty()) QTextStream(stderr) << error << Qt::endl;
        return error.isEmpty() ? 0 : 1;
    }
    if (arguments.contains(QStringLiteral("--uninstall"))) {
        const auto error = opennord::WindowsService::uninstall();
        if (!error.isEmpty()) QTextStream(stderr) << error << Qt::endl;
        return error.isEmpty() ? 0 : 1;
    }
    if (arguments.contains(QStringLiteral("--start"))) {
        const auto error = opennord::WindowsService::start();
        if (!error.isEmpty()) QTextStream(stderr) << error << Qt::endl;
        return error.isEmpty() ? 0 : 1;
    }
    if (arguments.contains(QStringLiteral("--stop"))) {
        const auto error = opennord::WindowsService::stop();
        if (!error.isEmpty()) QTextStream(stderr) << error << Qt::endl;
        return error.isEmpty() ? 0 : 1;
    }
    if (arguments.contains(QStringLiteral("--restart"))) {
        const auto error = opennord::WindowsService::restart();
        if (!error.isEmpty()) QTextStream(stderr) << error << Qt::endl;
        return error.isEmpty() ? 0 : 1;
    }

    opennord::VpnController controller;
    if (arguments.contains(QStringLiteral("--console"))) {
        opennord::NamedPipeServer server(controller);
        QString error;
        if (!server.start(error)) {
            QTextStream(stderr) << error << Qt::endl;
            return 1;
        }
        return application.exec();
    }
    return opennord::WindowsService::dispatch(controller);
}
