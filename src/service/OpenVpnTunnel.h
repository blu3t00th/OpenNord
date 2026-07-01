#pragma once

#include "common/Models.h"
#include "windows/WinHandle.h"

#include <QByteArray>
#include <QMutex>
#include <QString>

#include <condition_variable>
#include <atomic>
#include <mutex>
#include <thread>

namespace opennord {

class OpenVpnTunnel final
{
public:
    OpenVpnTunnel();
    ~OpenVpnTunnel();
    OpenVpnTunnel(const OpenVpnTunnel &) = delete;
    OpenVpnTunnel &operator=(const OpenVpnTunnel &) = delete;

    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool running() const;
    [[nodiscard]] bool connected() const;
    [[nodiscard]] QString connect(const Credentials &credentials, const Server &server,
                                  const Settings &settings, const QByteArray &signedProfile);
    [[nodiscard]] QString disconnect();
    [[nodiscard]] QString executable() const { return executable_; }

private:
    [[nodiscard]] quint16 reserveManagementPort() const;
    [[nodiscard]] QString launchProcess();
    void managementLoop(std::stop_token stopToken, QString username, QString password);
    void completeStartup(QString error);
    void cleanupFiles();

    QString executable_;
    QString directory_;
    QString configPath_;
    QString managementPasswordPath_;
    QString logPath_;
    QString managementPassword_;
    quint16 managementPort_{};

    mutable QMutex processMutex_;
    windows::UniqueHandle process_;
    windows::UniqueHandle job_;
    std::jthread managementThread_;
    std::mutex startupMutex_;
    std::condition_variable startupCondition_;
    bool startupComplete_{};
    QString startupError_;
    std::atomic_bool tunnelConnected_{};
};

}
