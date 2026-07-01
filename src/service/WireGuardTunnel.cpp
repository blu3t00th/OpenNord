#include "service/WireGuardTunnel.h"

#include "common/WireGuardConfig.h"
#include "windows/Security.h"
#include "windows/WinHandle.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QThread>

#include <windows.h>

namespace opennord {
namespace {
constexpr auto TunnelName = "OpenNord";
constexpr auto ServiceName = L"WireGuardTunnel$OpenNord";
}

WireGuardTunnel::WireGuardTunnel()
{
    executable_ = QDir(qEnvironmentVariable("ProgramFiles", QStringLiteral("C:/Program Files")))
        .filePath(QStringLiteral("WireGuard/wireguard.exe"));
    configPath_ = QDir(qEnvironmentVariable("ProgramData", QStringLiteral("C:/ProgramData")))
        .filePath(QStringLiteral("OpenNord/Tunnels/OpenNord.conf"));
}

bool WireGuardTunnel::ready() const
{
    const QFileInfo info(executable_);
    return info.exists() && info.isFile();
}

bool WireGuardTunnel::serviceExists() const
{
    windows::UniqueServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!manager) return false;
    windows::UniqueServiceHandle service(OpenServiceW(manager.get(), ServiceName, SERVICE_QUERY_STATUS));
    return static_cast<bool>(service);
}

bool WireGuardTunnel::connected() const
{
    windows::UniqueServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!manager) return false;
    windows::UniqueServiceHandle service(OpenServiceW(manager.get(), ServiceName, SERVICE_QUERY_STATUS));
    if (!service) return false;
    SERVICE_STATUS_PROCESS status{};
    DWORD required{};
    if (!QueryServiceStatusEx(service.get(), SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE *>(&status), sizeof(status), &required)) return false;
    return status.dwCurrentState == SERVICE_RUNNING || status.dwCurrentState == SERVICE_START_PENDING;
}

QString WireGuardTunnel::run(const QStringList &arguments, int timeoutMs) const
{
    QProcess process;
    process.setProgram(executable_);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(5000)) return QStringLiteral("cannot start WireGuard: %1").arg(process.errorString());
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        return QStringLiteral("WireGuard operation timed out");
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return QStringLiteral("WireGuard failed: %1").arg(QString::fromLocal8Bit(process.readAll()).trimmed());
    }
    return {};
}

QString WireGuardTunnel::connect(const Credentials &credentials, const Server &server, const Settings &settings)
{
    if (!ready()) return QStringLiteral("WireGuard for Windows is not installed");
    const auto config = buildWireGuardConfig(credentials, server, settings);
    if (!config.ok()) return config.error;
    if (serviceExists()) {
        const auto error = disconnect();
        if (!error.isEmpty()) return error;
    }
    if (!QDir().mkpath(QFileInfo(configPath_).absolutePath())) return QStringLiteral("cannot create tunnel directory");
    QSaveFile file(configPath_);
    const auto bytes = config.value.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return QStringLiteral("cannot write WireGuard configuration");
    }
    QString aclError;
    if (!windows::applyPrivateFileAcl(configPath_, {}, false, aclError)) {
        QFile::remove(configPath_);
        return aclError;
    }
    const auto installError = run({QStringLiteral("/installtunnelservice"), QDir::toNativeSeparators(configPath_)}, 20000);
    if (!installError.isEmpty()) {
        QFile::remove(configPath_);
        return installError;
    }
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 12000) {
        if (connected()) return {};
        QThread::msleep(200);
    }
    const auto rollbackError = disconnect();
    return rollbackError.isEmpty()
        ? QStringLiteral("WireGuard tunnel did not reach the running state")
        : QStringLiteral("WireGuard tunnel failed and rollback also failed: %1").arg(rollbackError);
}

QString WireGuardTunnel::disconnect()
{
    if (ready() && serviceExists()) {
        const auto error = run({QStringLiteral("/uninstalltunnelservice"), QString::fromLatin1(TunnelName)}, 20000);
        if (!error.isEmpty() && serviceExists()) return error;
    }
    if (QFile::exists(configPath_) && !QFile::remove(configPath_)) return QStringLiteral("cannot remove tunnel configuration");
    return {};
}

}
