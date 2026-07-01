#include "gui/RpcClient.h"

#include "common/Logging.h"
#include "common/Protocol.h"
#include "windows/Security.h"
#include "windows/WinHandle.h"

#include <QFutureWatcher>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QtConcurrent>
#include <QtEndian>

#include <windows.h>

#include <optional>

namespace opennord {
namespace {

struct ServiceAvailability {
    QString code;
    QString message;
    [[nodiscard]] bool ready() const { return message.isEmpty(); }
};

void log(const QString &message)
{
    logging::write(logging::Target::Gui, QStringLiteral("service-client"), message);
}

QString executableFromCommand(QString command)
{
    command = command.trimmed();
    if (command.startsWith(u'"')) {
        const auto closingQuote = command.indexOf(u'"', 1);
        if (closingQuote > 1) return command.mid(1, closingQuote - 1);
    }
    return command.section(u' ', 0, 0);
}

ServiceAvailability ensureServiceRunning(bool allowStart)
{
    windows::UniqueServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!manager) {
        const auto message = QStringLiteral("Cannot access the Windows Service Control Manager: %1")
            .arg(windows::lastErrorMessage());
        log(message);
        return {QStringLiteral("service_status_failed"), message};
    }

    windows::UniqueServiceHandle service(OpenServiceW(manager.get(), protocol::ServiceName, SERVICE_QUERY_STATUS));
    if (!service) {
        if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
            const auto packagedService = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("OpenNordService.exe"));
            const auto message = QFileInfo::exists(packagedService)
                ? QStringLiteral("OpenNord service is not installed. Please reinstall OpenNord as administrator.")
                : QStringLiteral("OpenNord service executable is missing. Please reinstall OpenNord as administrator.");
            log(message);
            return {QFileInfo::exists(packagedService) ? QStringLiteral("service_not_installed")
                                                       : QStringLiteral("service_executable_missing"), message};
        }
        const auto message = QStringLiteral("Cannot query the OpenNord service: %1").arg(windows::lastErrorMessage());
        log(message);
        return {QStringLiteral("service_status_failed"), message};
    }

    windows::UniqueServiceHandle configurationService(OpenServiceW(manager.get(), protocol::ServiceName, SERVICE_QUERY_CONFIG));
    DWORD configSize{};
    if (configurationService) QueryServiceConfigW(configurationService.get(), nullptr, 0, &configSize);
    if (configurationService && GetLastError() == ERROR_INSUFFICIENT_BUFFER && configSize > 0) {
        QByteArray buffer(static_cast<qsizetype>(configSize), Qt::Uninitialized);
        auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buffer.data());
        if (QueryServiceConfigW(configurationService.get(), config, configSize, &configSize)) {
            const auto executable = executableFromCommand(QString::fromWCharArray(config->lpBinaryPathName));
            if (executable.isEmpty() || !QFileInfo::exists(executable)) {
                const auto message = QStringLiteral("The registered OpenNord service executable is missing at %1. Please reinstall OpenNord as administrator.")
                    .arg(executable.isEmpty() ? QStringLiteral("an unknown path") : QDir::toNativeSeparators(executable));
                log(message);
                return {QStringLiteral("service_executable_missing"), message};
            }
        }
    }

    const auto queryStatus = [&]() -> std::optional<DWORD> {
        SERVICE_STATUS_PROCESS status{};
        DWORD required{};
        if (!QueryServiceStatusEx(service.get(), SC_STATUS_PROCESS_INFO,
                reinterpret_cast<BYTE *>(&status), sizeof(status), &required)) return std::nullopt;
        return status.dwCurrentState;
    };

    auto state = queryStatus();
    if (!state.has_value()) {
        const auto message = QStringLiteral("Cannot read the OpenNord service status: %1").arg(windows::lastErrorMessage());
        log(message);
        return {QStringLiteral("service_status_failed"), message};
    }
    if (*state == SERVICE_RUNNING) {
        return {};
    }

    if (*state == SERVICE_START_PENDING || *state == SERVICE_STOP_PENDING) {
        const auto started = GetTickCount64();
        while (GetTickCount64() - started < 5000) {
            Sleep(100);
            state = queryStatus();
            if (state.has_value() && *state == SERVICE_RUNNING) {
                log(QStringLiteral("service reached running state"));
                return {};
            }
            if (!state.has_value() || *state == SERVICE_STOPPED) break;
        }
    }

    if (state.has_value() && *state == SERVICE_STOPPED) {
        if (!allowStart) {
            const auto message = QStringLiteral("OpenNord service is stopped. Use the tray icon to start it again.");
            log(message);
            return {QStringLiteral("service_stopped"), message};
        }
        log(QStringLiteral("service is stopped; attempting restart"));
        windows::UniqueServiceHandle starter(OpenServiceW(manager.get(), protocol::ServiceName,
            SERVICE_START | SERVICE_QUERY_STATUS));
        if (!starter) {
            const auto accessDenied = GetLastError() == ERROR_ACCESS_DENIED;
            const auto message = accessDenied
                ? QStringLiteral("OpenNord service is stopped and requires administrator permission to start. Please start or reinstall OpenNord as administrator.")
                : QStringLiteral("OpenNord service is stopped and could not be opened for restart: %1").arg(windows::lastErrorMessage());
            log(message);
            return {accessDenied ? QStringLiteral("service_start_permission_denied") : QStringLiteral("service_start_failed"), message};
        }
        if (!StartServiceW(starter.get(), 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
            const auto accessDenied = GetLastError() == ERROR_ACCESS_DENIED;
            const auto message = accessDenied
                ? QStringLiteral("OpenNord service is stopped and requires administrator permission to start. Please start or reinstall OpenNord as administrator.")
                : QStringLiteral("OpenNord service failed to start: %1").arg(windows::lastErrorMessage());
            log(message);
            return {accessDenied ? QStringLiteral("service_start_permission_denied") : QStringLiteral("service_start_failed"), message};
        }
        const auto started = GetTickCount64();
        while (GetTickCount64() - started < 10000) {
            SERVICE_STATUS_PROCESS status{};
            DWORD required{};
            if (!QueryServiceStatusEx(starter.get(), SC_STATUS_PROCESS_INFO,
                    reinterpret_cast<BYTE *>(&status), sizeof(status), &required)) break;
            if (status.dwCurrentState == SERVICE_RUNNING) {
                log(QStringLiteral("service restart succeeded"));
                return {};
            }
            if (status.dwCurrentState == SERVICE_STOPPED) break;
            Sleep(100);
        }
        const auto message = QStringLiteral("OpenNord service did not reach the running state after restart. Please reinstall OpenNord as administrator.");
        log(message);
        return {QStringLiteral("service_start_failed"), message};
    }

    const auto message = QStringLiteral("OpenNord service is not running (Windows state %1). Please restart or reinstall OpenNord as administrator.")
        .arg(state.value_or(0));
    log(message);
    return {QStringLiteral("service_not_running"), message};
}

bool writeAll(HANDLE pipe, const QByteArray &data)
{
    DWORD offset{};
    while (offset < static_cast<DWORD>(data.size())) {
        DWORD written{};
        if (!WriteFile(pipe, data.constData() + offset, static_cast<DWORD>(data.size()) - offset, &written, nullptr)) return false;
        offset += written;
    }
    return true;
}

bool readExact(HANDLE pipe, char *data, DWORD size)
{
    DWORD offset{};
    while (offset < size) {
        DWORD read{};
        if (!ReadFile(pipe, data + offset, size - offset, &read, nullptr) || read == 0) return false;
        offset += read;
    }
    return true;
}

QJsonObject localFailure(QString code, QString message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), std::move(code)}, {QStringLiteral("message"), std::move(message)}}}};
}

}

RpcClient::RpcClient(QObject *parent) : QObject(parent) {}

void RpcClient::call(QString method, QJsonObject params, Callback callback)
{
    const auto id = nextId_++;
    auto *watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [watcher, callback = std::move(callback)]() mutable {
        const auto response = watcher->result();
        watcher->deleteLater();
        if (response.value(QStringLiteral("ok")).toBool()) {
            callback(response.value(QStringLiteral("result")), {});
        } else {
            callback({}, response.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString(QStringLiteral("service request failed")));
        }
    });
    watcher->setFuture(QtConcurrent::run([this, id, method = std::move(method), params = std::move(params)] {
        return callBlocking(id, method, params);
    }));
}

QJsonObject RpcClient::callBlocking(qint64 id, const QString &method, const QJsonObject &params,
                                    bool allowStatusRetry) const
{
    const auto routineStatus = method == QStringLiteral("status");
    if (!routineStatus) log(QStringLiteral("RPC %1: checking service status").arg(method));
    const auto availability = ensureServiceRunning(autoStartService_.load());
    if (!availability.ready()) return localFailure(availability.code, availability.message);

    if (!routineStatus) log(QStringLiteral("RPC %1: waiting for named pipe").arg(method));
    if (!WaitNamedPipeW(protocol::PipeName, 5000)) {
        if (routineStatus && allowStatusRetry) {
            log(QStringLiteral("status IPC was unavailable during startup; retrying once"));
            Sleep(300);
            return callBlocking(id, method, params, false);
        }
        const auto message = QStringLiteral("OpenNord service is running, but its IPC named pipe is unavailable: %1")
            .arg(windows::lastErrorMessage());
        log(QStringLiteral("RPC %1: %2").arg(method, message));
        return localFailure(QStringLiteral("ipc_unavailable"), message);
    }
    windows::UniqueHandle pipe(CreateFileW(protocol::PipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!pipe) {
        if (routineStatus && allowStatusRetry) {
            log(QStringLiteral("status IPC connection failed during startup; retrying once"));
            Sleep(300);
            return callBlocking(id, method, params, false);
        }
        const auto message = QStringLiteral("Cannot open the OpenNord IPC named pipe: %1").arg(windows::lastErrorMessage());
        log(QStringLiteral("RPC %1: %2").arg(method, message));
        return localFailure(QStringLiteral("ipc_connection_failed"), message);
    }
    const auto frame = protocol::encodeFrame({{QStringLiteral("id"), id}, {QStringLiteral("method"), method}, {QStringLiteral("params"), params}});
    if (!writeAll(pipe.get(), frame)) {
        log(QStringLiteral("RPC %1: request write failed").arg(method));
        return localFailure(QStringLiteral("ipc_write_failed"), QStringLiteral("Cannot send the request to the OpenNord service."));
    }

    quint32 encodedLength{};
    if (!readExact(pipe.get(), reinterpret_cast<char *>(&encodedLength), sizeof(encodedLength))) {
        if (routineStatus && allowStatusRetry) {
            log(QStringLiteral("status IPC connection closed during startup; retrying once"));
            Sleep(300);
            return callBlocking(id, method, params, false);
        }
        log(QStringLiteral("RPC %1: service closed the connection before responding").arg(method));
        return localFailure(QStringLiteral("ipc_connection_closed"), QStringLiteral("OpenNord service closed the IPC connection before responding."));
    }
    const auto length = qFromLittleEndian(encodedLength);
    if (length == 0 || length > protocol::MaxFrameSize) return localFailure(QStringLiteral("ipc_invalid_response"), QStringLiteral("OpenNord service returned an invalid response size."));
    QByteArray payload(static_cast<qsizetype>(length), Qt::Uninitialized);
    if (!readExact(pipe.get(), payload.data(), length)) return localFailure(QStringLiteral("ipc_incomplete_response"), QStringLiteral("OpenNord service returned an incomplete response."));
    QJsonParseError parseError;
    const auto response = QJsonDocument::fromJson(payload, &parseError).object();
    if (parseError.error != QJsonParseError::NoError || response.value(QStringLiteral("id")).toInteger() != id) {
        return localFailure(QStringLiteral("ipc_invalid_response"), QStringLiteral("OpenNord service returned an invalid response."));
    }
    const auto error = response.value(QStringLiteral("error")).toObject();
    if (!response.value(QStringLiteral("ok")).toBool()) {
        log(QStringLiteral("RPC %1 failed with %2: %3").arg(method,
            error.value(QStringLiteral("code")).toString(), error.value(QStringLiteral("message")).toString()));
    } else if (!routineStatus) {
        log(QStringLiteral("RPC %1 completed successfully").arg(method));
    }
    return response;
}

}
