#include "common/Logging.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QTextStream>

namespace opennord::logging {
namespace {

QMutex logMutex;

QString clean(QString value)
{
    value.replace(u'\r', u' ');
    value.replace(u'\n', u' ');
    return value;
}

}

QString path(Target target)
{
    const auto root = target == Target::Service
        ? qEnvironmentVariable("ProgramData", QStringLiteral("C:/ProgramData"))
        : qEnvironmentVariable("LOCALAPPDATA", QDir::homePath());
    return QDir(root).filePath(target == Target::Service
        ? QStringLiteral("OpenNord/service.log")
        : QStringLiteral("OpenNord/gui.log"));
}

void write(Target target, const QString &area, const QString &message)
{
    QMutexLocker locker(&logMutex);
    const auto logPath = path(target);
    QDir().mkpath(QFileInfo(logPath).absolutePath());
    if (QFileInfo(logPath).size() > 2 * 1024 * 1024) {
        const auto previousPath = logPath + QStringLiteral(".1");
        QFile::remove(previousPath);
        QFile::rename(logPath, previousPath);
    }
    QFile file(logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    QTextStream stream(&file);
    stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
           << " pid=" << QCoreApplication::applicationPid()
           << " [" << clean(area) << "] " << clean(message) << Qt::endl;
}

}
