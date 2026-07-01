#include "service/OpenVpnTunnel.h"

#include "common/OpenVpnConfig.h"
#include "windows/Security.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>

#include <windows.h>

#include <array>
#include <chrono>
#include <system_error>
#include <vector>

namespace opennord {
namespace {

QString quoteArgument(const QString &value)
{
    auto quoted = value;
    quoted.replace(u'"', QStringLiteral("\\\""));
    return u'"' + quoted + u'"';
}

bool writeSocketLine(QTcpSocket &socket, const QString &line)
{
    const auto data = line.toUtf8() + '\n';
    return socket.write(data) == data.size() && socket.waitForBytesWritten(3000);
}

}

OpenVpnTunnel::OpenVpnTunnel()
{
    executable_ = QDir(qEnvironmentVariable("ProgramFiles", QStringLiteral("C:/Program Files")))
        .filePath(QStringLiteral("OpenVPN/bin/openvpn.exe"));
    directory_ = QDir(qEnvironmentVariable("ProgramData", QStringLiteral("C:/ProgramData")))
        .filePath(QStringLiteral("OpenNord/OpenVPN"));
    configPath_ = QDir(directory_).filePath(QStringLiteral("OpenNord.ovpn"));
    managementPasswordPath_ = QDir(directory_).filePath(QStringLiteral("management.key"));
    logPath_ = QDir(directory_).filePath(QStringLiteral("openvpn.log"));
}

OpenVpnTunnel::~OpenVpnTunnel() { (void)disconnect(); }

bool OpenVpnTunnel::ready() const
{
    const QFileInfo info(executable_);
    return info.exists() && info.isFile();
}

bool OpenVpnTunnel::running() const
{
    QMutexLocker locker(&processMutex_);
    if (!process_) return false;
    DWORD exitCode{};
    return GetExitCodeProcess(process_.get(), &exitCode) && exitCode == STILL_ACTIVE;
}

bool OpenVpnTunnel::connected() const
{
    return tunnelConnected_.load(std::memory_order_acquire) && running();
}

quint16 OpenVpnTunnel::reserveManagementPort() const
{
    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::LocalHost, 0)) return 0;
    return reservation.serverPort();
}

QString OpenVpnTunnel::connect(const Credentials &credentials, const Server &server,
                               const Settings &settings, const QByteArray &signedProfile)
{
    if (!ready()) return QStringLiteral("OpenVPN Community is not installed");
    if (credentials.username.isEmpty() || credentials.password.isEmpty()) {
        return QStringLiteral("Nord OpenVPN service credentials are missing");
    }
    if (running()) {
        const auto error = disconnect();
        if (!error.isEmpty()) return error;
    }
    if (!QDir().mkpath(directory_)) return QStringLiteral("cannot create OpenVPN state directory");
    managementPort_ = reserveManagementPort();
    if (managementPort_ == 0) return QStringLiteral("cannot reserve a local OpenVPN management port");
    std::array<char, 32> randomBytes{};
    for (auto &byte : randomBytes) byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xffU);
    managementPassword_ = QString::fromLatin1(QByteArray(randomBytes.data(), randomBytes.size()).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));

    const auto config = buildOpenVpnConfig(signedProfile, server, settings, managementPort_, managementPasswordPath_);
    if (!config.ok()) return config.error;
    QSaveFile passwordFile(managementPasswordPath_);
    const auto passwordBytes = managementPassword_.toUtf8() + '\n';
    if (!passwordFile.open(QIODevice::WriteOnly) || passwordFile.write(passwordBytes) != passwordBytes.size() || !passwordFile.commit()) {
        return QStringLiteral("cannot write OpenVPN management password");
    }
    QSaveFile configFile(configPath_);
    const auto configBytes = config.value.toUtf8();
    if (!configFile.open(QIODevice::WriteOnly) || configFile.write(configBytes) != configBytes.size() || !configFile.commit()) {
        cleanupFiles();
        return QStringLiteral("cannot write OpenVPN configuration");
    }
    QString aclError;
    if (!windows::applyPrivateFileAcl(managementPasswordPath_, {}, false, aclError)
        || !windows::applyPrivateFileAcl(configPath_, {}, false, aclError)) {
        cleanupFiles();
        return aclError;
    }
    const auto launchError = launchProcess();
    if (!launchError.isEmpty()) {
        cleanupFiles();
        return launchError;
    }

    tunnelConnected_.store(false, std::memory_order_release);
    {
        std::scoped_lock lock(startupMutex_);
        startupComplete_ = false;
        startupError_.clear();
    }
    try {
        managementThread_ = std::jthread([this, username = credentials.username, password = credentials.password](std::stop_token token) mutable {
            managementLoop(token, std::move(username), std::move(password));
        });
    } catch (const std::system_error &error) {
        const auto stopError = disconnect();
        const auto message = QStringLiteral("cannot start OpenVPN management worker: %1").arg(QString::fromLocal8Bit(error.what()));
        return stopError.isEmpty() ? message : QStringLiteral("%1; rollback failed: %2").arg(message, stopError);
    }
    std::unique_lock lock(startupMutex_);
    if (!startupCondition_.wait_for(lock, std::chrono::seconds(45), [this] { return startupComplete_; })) {
        lock.unlock();
        const auto stopError = disconnect();
        return stopError.isEmpty() ? QStringLiteral("OpenVPN connection timed out")
                                   : QStringLiteral("OpenVPN timed out and rollback failed: %1").arg(stopError);
    }
    const auto result = startupError_;
    lock.unlock();
    QFile::remove(managementPasswordPath_);
    if (!result.isEmpty()) {
        const auto stopError = disconnect();
        return stopError.isEmpty() ? result : QStringLiteral("%1; rollback failed: %2").arg(result, stopError);
    }
    return {};
}

QString OpenVpnTunnel::launchProcess()
{
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    windows::UniqueHandle log(CreateFileW(reinterpret_cast<LPCWSTR>(logPath_.utf16()), GENERIC_WRITE, FILE_SHARE_READ,
        &inheritable, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    windows::UniqueHandle nullInput(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &inheritable, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!log || !nullInput) return QStringLiteral("cannot open OpenVPN process streams: %1").arg(windows::lastErrorMessage());
    QString aclError;
    if (!windows::applyPrivateFileAcl(logPath_, {}, false, aclError)) return aclError;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nullInput.get();
    startup.hStdOutput = log.get();
    startup.hStdError = log.get();
    PROCESS_INFORMATION processInfo{};
    auto command = quoteArgument(executable_) + QStringLiteral(" --config ") + quoteArgument(configPath_);
    std::vector<wchar_t> mutableCommand(command.size() + 1);
    command.toWCharArray(mutableCommand.data());
    mutableCommand[command.size()] = L'\0';
    if (!CreateProcessW(reinterpret_cast<LPCWSTR>(executable_.utf16()), mutableCommand.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, nullptr, reinterpret_cast<LPCWSTR>(directory_.utf16()),
            &startup, &processInfo)) {
        return QStringLiteral("cannot launch OpenVPN: %1").arg(windows::lastErrorMessage());
    }
    windows::UniqueHandle thread(processInfo.hThread);
    windows::UniqueHandle process(processInfo.hProcess);
    windows::UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    if (!job) {
        TerminateProcess(process.get(), ERROR_PROCESS_ABORTED);
        return QStringLiteral("cannot create OpenVPN Job Object: %1").arg(windows::lastErrorMessage());
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))
        || !AssignProcessToJobObject(job.get(), process.get())) {
        TerminateProcess(process.get(), ERROR_PROCESS_ABORTED);
        return QStringLiteral("cannot contain OpenVPN process: %1").arg(windows::lastErrorMessage());
    }
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        TerminateProcess(process.get(), ERROR_PROCESS_ABORTED);
        return QStringLiteral("cannot start contained OpenVPN process: %1").arg(windows::lastErrorMessage());
    }
    QMutexLocker locker(&processMutex_);
    process_ = std::move(process);
    job_ = std::move(job);
    return {};
}

void OpenVpnTunnel::completeStartup(QString error)
{
    std::scoped_lock lock(startupMutex_);
    if (startupComplete_) return;
    startupError_ = std::move(error);
    startupComplete_ = true;
    startupCondition_.notify_all();
}

void OpenVpnTunnel::managementLoop(std::stop_token stopToken, QString username, QString password)
{
    const auto clearCredentials = qScopeGuard([&] {
        password.fill(u'\0');
        username.fill(u'\0');
    });
    const auto terminateUnmanagedProcess = qScopeGuard([&] {
        tunnelConnected_.store(false, std::memory_order_release);
        if (!stopToken.stop_requested()) {
            QMutexLocker locker(&processMutex_);
            if (process_) TerminateProcess(process_.get(), ERROR_PROCESS_ABORTED);
        }
    });
    QTcpSocket socket;
    QByteArray buffer;
    while (!stopToken.stop_requested()) {
        socket.connectToHost(QHostAddress::LocalHost, managementPort_);
        if (socket.waitForConnected(500)) break;
        socket.abort();
        if (!running()) {
            completeStartup(QStringLiteral("OpenVPN exited before management became available"));
            return;
        }
    }
    if (stopToken.stop_requested()) return;
    if (!writeSocketLine(socket, managementPassword_)
        || !writeSocketLine(socket, QStringLiteral("state on"))
        || !writeSocketLine(socket, QStringLiteral("hold release"))) {
        completeStartup(QStringLiteral("cannot initialize OpenVPN management channel"));
        return;
    }

    while (!stopToken.stop_requested()) {
        if (!socket.waitForReadyRead(300)) {
            if (socket.state() != QAbstractSocket::ConnectedState || !running()) break;
            continue;
        }
        buffer.append(socket.readAll());
        qsizetype newline{};
        while ((newline = buffer.indexOf('\n')) >= 0) {
            const auto line = QString::fromUtf8(buffer.first(newline)).trimmed();
            buffer.remove(0, newline + 1);
            if (line.contains(QStringLiteral(">PASSWORD:Need 'Auth' username/password"))) {
                if (!writeSocketLine(socket, QStringLiteral("username \"Auth\" %1").arg(openVpnManagementEscape(username)))
                    || !writeSocketLine(socket, QStringLiteral("password \"Auth\" %1").arg(openVpnManagementEscape(password)))) {
                    completeStartup(QStringLiteral("cannot provide OpenVPN service credentials"));
                    return;
                }
            } else if (line.contains(QStringLiteral("AUTH_FAILED")) || line.contains(QStringLiteral(">PASSWORD:Verification Failed"))) {
                tunnelConnected_.store(false, std::memory_order_release);
                completeStartup(QStringLiteral("Nord OpenVPN service credentials were rejected"));
                return;
            } else if (line.startsWith(QStringLiteral(">FATAL:"))) {
                tunnelConnected_.store(false, std::memory_order_release);
                completeStartup(line.mid(7).trimmed());
                return;
            } else if (line.startsWith(QStringLiteral(">STATE:"))) {
                const auto connectedState = line.contains(QStringLiteral(",CONNECTED,SUCCESS,"));
                tunnelConnected_.store(connectedState, std::memory_order_release);
                if (connectedState) completeStartup({});
            }
        }
    }
    if (stopToken.stop_requested() && socket.state() == QAbstractSocket::ConnectedState) {
        writeSocketLine(socket, QStringLiteral("signal SIGTERM"));
        socket.waitForBytesWritten(1000);
    } else {
        completeStartup(QStringLiteral("OpenVPN management channel closed before connecting"));
    }
}

QString OpenVpnTunnel::disconnect()
{
    if (managementThread_.joinable()) {
        managementThread_.request_stop();
        managementThread_.join();
    }
    HANDLE processHandle{};
    {
        QMutexLocker locker(&processMutex_);
        processHandle = process_.get();
    }
    QString error;
    bool stopped = !processHandle;
    if (processHandle) {
        auto waitResult = WaitForSingleObject(processHandle, 10000);
        if (waitResult == WAIT_TIMEOUT) {
            (void)TerminateProcess(processHandle, ERROR_PROCESS_ABORTED);
            waitResult = WaitForSingleObject(processHandle, 3000);
        }
        if (waitResult == WAIT_TIMEOUT) {
            QMutexLocker locker(&processMutex_);
            job_.reset();
            waitResult = WaitForSingleObject(processHandle, 3000);
        }
        stopped = waitResult == WAIT_OBJECT_0;
        if (!stopped) error = waitResult == WAIT_FAILED
            ? QStringLiteral("cannot wait for OpenVPN to stop: %1").arg(windows::lastErrorMessage())
            : QStringLiteral("OpenVPN did not stop after management, process, and Job Object termination");
    }
    if (stopped) {
        QMutexLocker locker(&processMutex_);
        process_.reset();
        job_.reset();
    }
    managementPassword_.fill(u'\0');
    managementPassword_.clear();
    managementPort_ = 0;
    tunnelConnected_.store(false, std::memory_order_release);
    if (stopped) cleanupFiles();
    return error;
}

void OpenVpnTunnel::cleanupFiles()
{
    QFile::remove(configPath_);
    QFile::remove(managementPasswordPath_);
}

}
