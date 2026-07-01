#include "service/WindowsService.h"

#include "common/Logging.h"
#include "service/NamedPipeServer.h"
#include "windows/Security.h"
#include "windows/WinHandle.h"

#include <QCoreApplication>
#include <QDir>

#include <windows.h>

namespace opennord {
namespace {

VpnController *activeController{};
SERVICE_STATUS_HANDLE statusHandle{};
SERVICE_STATUS serviceStatus{};
windows::UniqueHandle stopEvent;

void log(const QString &message)
{
    logging::write(logging::Target::Service, QStringLiteral("windows-service"), message);
}

bool waitForState(SC_HANDLE service, DWORD expectedState, DWORD timeoutMs, QString &error)
{
    const auto started = GetTickCount64();
    while (GetTickCount64() - started < timeoutMs) {
        SERVICE_STATUS_PROCESS status{};
        DWORD required{};
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                reinterpret_cast<BYTE *>(&status), sizeof(status), &required)) {
            error = QStringLiteral("cannot query service status: %1").arg(windows::lastErrorMessage());
            return false;
        }
        if (status.dwCurrentState == expectedState) return true;
        if (status.dwCurrentState == SERVICE_STOPPED && expectedState != SERVICE_STOPPED) {
            error = QStringLiteral("service stopped during startup with Windows error %1").arg(status.dwWin32ExitCode);
            return false;
        }
        Sleep(100);
    }
    error = QStringLiteral("service did not reach the expected state before timeout");
    return false;
}

bool configureRecovery(SC_HANDLE service, QString &error)
{
    SC_ACTION actions[] = {
        {SC_ACTION_RESTART, 1000},
        {SC_ACTION_RESTART, 5000},
        {SC_ACTION_RESTART, 15000},
    };
    SERVICE_FAILURE_ACTIONSW recovery{};
    recovery.dwResetPeriod = 86400;
    recovery.cActions = static_cast<DWORD>(std::size(actions));
    recovery.lpsaActions = actions;
    if (!ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &recovery)) {
        error = QStringLiteral("cannot configure service recovery: %1").arg(windows::lastErrorMessage());
        return false;
    }
    SERVICE_FAILURE_ACTIONS_FLAG failureFlag{TRUE};
    if (!ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &failureFlag)) {
        error = QStringLiteral("cannot enable service recovery: %1").arg(windows::lastErrorMessage());
        return false;
    }
    return true;
}

void publishStatus(DWORD state, DWORD error = NO_ERROR, DWORD hint = 0)
{
    serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    serviceStatus.dwCurrentState = state;
    serviceStatus.dwWin32ExitCode = error;
    serviceStatus.dwWaitHint = hint;
    serviceStatus.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    SetServiceStatus(statusHandle, &serviceStatus);
}

DWORD WINAPI controlHandler(DWORD control, DWORD, void *, void *)
{
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        publishStatus(SERVICE_STOP_PENDING, NO_ERROR, 10000);
        if (stopEvent) SetEvent(stopEvent.get());
    }
    return NO_ERROR;
}

void WINAPI serviceMain(DWORD, LPWSTR *)
{
    log(QStringLiteral("service process entered ServiceMain"));
    statusHandle = RegisterServiceCtrlHandlerExW(WindowsService::Name, controlHandler, nullptr);
    if (!statusHandle) {
        log(QStringLiteral("RegisterServiceCtrlHandlerEx failed: %1").arg(windows::lastErrorMessage()));
        return;
    }
    publishStatus(SERVICE_START_PENDING, NO_ERROR, 5000);
    stopEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!stopEvent) {
        log(QStringLiteral("cannot create stop event: %1").arg(windows::lastErrorMessage()));
        publishStatus(SERVICE_STOPPED, GetLastError());
        return;
    }
    NamedPipeServer server(*activeController);
    QString error;
    if (!server.start(error)) {
        log(QStringLiteral("named-pipe server failed to start: %1").arg(error));
        publishStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
        return;
    }
    publishStatus(SERVICE_RUNNING);
    log(QStringLiteral("service is running and IPC is ready"));
    WaitForSingleObject(stopEvent.get(), INFINITE);
    server.stop();
    activeController->shutdown();
    publishStatus(SERVICE_STOPPED);
    log(QStringLiteral("service stopped cleanly"));
}

}

int WindowsService::dispatch(VpnController &controller)
{
    activeController = &controller;
    SERVICE_TABLE_ENTRYW table[] = {
        {const_cast<LPWSTR>(Name), serviceMain},
        {nullptr, nullptr},
    };
    if (!StartServiceCtrlDispatcherW(table)) {
        const auto error = GetLastError();
        log(QStringLiteral("StartServiceCtrlDispatcher failed: %1").arg(windows::lastErrorMessage(error)));
        return static_cast<int>(error);
    }
    return 0;
}

QString WindowsService::install()
{
    log(QStringLiteral("service installation requested"));
    windows::UniqueServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE));
    if (!manager) {
        const auto error = QStringLiteral("cannot open Service Control Manager: %1").arg(windows::lastErrorMessage());
        log(error);
        return error;
    }
    const auto executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const auto command = QStringLiteral("\"%1\"").arg(executable);
    constexpr DWORD serviceAccess = SERVICE_CHANGE_CONFIG | SERVICE_START | SERVICE_QUERY_STATUS | SERVICE_STOP | DELETE;
    windows::UniqueServiceHandle service(OpenServiceW(manager.get(), Name, serviceAccess));
    if (!service) {
        if (GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST) {
            const auto error = QStringLiteral("cannot open existing service: %1").arg(windows::lastErrorMessage());
            log(error);
            return error;
        }
        service.reset(CreateServiceW(
            manager.get(), Name, L"OpenNord VPN Service", serviceAccess,
            SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
            reinterpret_cast<LPCWSTR>(command.utf16()),
            nullptr, nullptr, nullptr, nullptr, nullptr));
        if (!service) {
            const auto error = QStringLiteral("cannot install service: %1").arg(windows::lastErrorMessage());
            log(error);
            return error;
        }
        log(QStringLiteral("service registered at %1").arg(executable));
    } else {
        if (!ChangeServiceConfigW(service.get(), SERVICE_NO_CHANGE, SERVICE_AUTO_START, SERVICE_NO_CHANGE,
                reinterpret_cast<LPCWSTR>(command.utf16()), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)) {
            const auto error = QStringLiteral("cannot update service executable path: %1").arg(windows::lastErrorMessage());
            log(error);
            return error;
        }
        log(QStringLiteral("existing service registration updated to %1").arg(executable));
    }
    SERVICE_DESCRIPTIONW description{const_cast<LPWSTR>(L"Open-source NordLynx controller for OpenNord")};
    ChangeServiceConfig2W(service.get(), SERVICE_CONFIG_DESCRIPTION, &description);
    QString recoveryError;
    if (!configureRecovery(service.get(), recoveryError)) {
        log(QStringLiteral("service recovery warning: %1").arg(recoveryError));
    }
    if (!StartServiceW(service.get(), 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
        const auto error = QStringLiteral("service installed but could not start: %1").arg(windows::lastErrorMessage());
        log(error);
        return error;
    }
    QString statusError;
    if (!waitForState(service.get(), SERVICE_RUNNING, 15000, statusError)) {
        log(statusError);
        return statusError;
    }
    log(QStringLiteral("service installation completed and service is running"));
    return {};
}

QString WindowsService::uninstall()
{
    log(QStringLiteral("service removal requested"));
    windows::UniqueServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!manager) return QStringLiteral("cannot open Service Control Manager: %1").arg(windows::lastErrorMessage());
    windows::UniqueServiceHandle service(OpenServiceW(manager.get(), Name, SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS));
    if (!service) return GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST ? QString() : windows::lastErrorMessage();
    SERVICE_STATUS status{};
    ControlService(service.get(), SERVICE_CONTROL_STOP, &status);
    for (int attempt = 0; attempt < 50; ++attempt) {
        SERVICE_STATUS_PROCESS processStatus{};
        DWORD required{};
        if (!QueryServiceStatusEx(service.get(), SC_STATUS_PROCESS_INFO,
                reinterpret_cast<BYTE *>(&processStatus), sizeof(processStatus), &required)
            || processStatus.dwCurrentState == SERVICE_STOPPED) {
            break;
        }
        Sleep(200);
    }
    if (!DeleteService(service.get()) && GetLastError() != ERROR_SERVICE_MARKED_FOR_DELETE) {
        const auto error = QStringLiteral("cannot remove service: %1").arg(windows::lastErrorMessage());
        log(error);
        return error;
    }
    log(QStringLiteral("service removed"));
    return {};
}

QString WindowsService::start()
{
    log(QStringLiteral("service start requested"));
    windows::UniqueServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!manager) return QStringLiteral("cannot open Service Control Manager: %1").arg(windows::lastErrorMessage());
    windows::UniqueServiceHandle service(OpenServiceW(manager.get(), Name, SERVICE_START | SERVICE_QUERY_STATUS));
    if (!service) {
        return GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST
            ? QStringLiteral("OpenNord service is not installed")
            : QStringLiteral("cannot open OpenNord service: %1").arg(windows::lastErrorMessage());
    }
    if (!StartServiceW(service.get(), 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
        const auto error = QStringLiteral("cannot start OpenNord service: %1").arg(windows::lastErrorMessage());
        log(error);
        return error;
    }
    QString error;
    if (!waitForState(service.get(), SERVICE_RUNNING, 15000, error)) {
        log(error);
        return error;
    }
    log(QStringLiteral("service start completed"));
    return {};
}

QString WindowsService::stop()
{
    log(QStringLiteral("service stop requested"));
    windows::UniqueServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!manager) return QStringLiteral("cannot open Service Control Manager: %1").arg(windows::lastErrorMessage());
    windows::UniqueServiceHandle service(OpenServiceW(manager.get(), Name, SERVICE_STOP | SERVICE_QUERY_STATUS));
    if (!service) {
        return GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST
            ? QStringLiteral("OpenNord service is not installed")
            : QStringLiteral("cannot open OpenNord service: %1").arg(windows::lastErrorMessage());
    }
    SERVICE_STATUS_PROCESS status{};
    DWORD required{};
    if (!QueryServiceStatusEx(service.get(), SC_STATUS_PROCESS_INFO,
            reinterpret_cast<BYTE *>(&status), sizeof(status), &required)) {
        return QStringLiteral("cannot query OpenNord service: %1").arg(windows::lastErrorMessage());
    }
    if (status.dwCurrentState != SERVICE_STOPPED) {
        SERVICE_STATUS controlStatus{};
        if (!ControlService(service.get(), SERVICE_CONTROL_STOP, &controlStatus)
            && GetLastError() != ERROR_SERVICE_NOT_ACTIVE) {
            const auto error = QStringLiteral("cannot stop OpenNord service: %1").arg(windows::lastErrorMessage());
            log(error);
            return error;
        }
        QString error;
        if (!waitForState(service.get(), SERVICE_STOPPED, 15000, error)) {
            log(error);
            return error;
        }
    }
    log(QStringLiteral("service stop completed"));
    return {};
}

QString WindowsService::restart()
{
    log(QStringLiteral("service restart requested"));
    const auto stopError = stop();
    if (!stopError.isEmpty()) return stopError;
    return start();
}

}
