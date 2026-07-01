#pragma once

#include "api/NordApiClient.h"
#include "common/Models.h"
#include "service/ClientContext.h"
#include "service/OpenVpnTunnel.h"
#include "service/SecureStore.h"
#include "service/WireGuardTunnel.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QHash>
#include <QMutex>

namespace opennord {

struct ControllerReply {
    QJsonValue value;
    QString code;
    QString error;
    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

class VpnController final
{
public:
    VpnController();

    [[nodiscard]] ControllerReply handle(const ClientContext &client, const QString &method, const QJsonObject &params);
    void shutdown();

private:
    [[nodiscard]] ControllerReply status(const ClientContext &client) const;
    [[nodiscard]] ControllerReply login(const ClientContext &client, const QJsonObject &params);
    [[nodiscard]] ControllerReply logout(const ClientContext &client);
    [[nodiscard]] ControllerReply locations(const ClientContext &client);
    [[nodiscard]] ControllerReply servers(const ClientContext &client, const QJsonObject &params);
    [[nodiscard]] ControllerReply connect(const ClientContext &client, const QJsonObject &params, bool quick);
    [[nodiscard]] ControllerReply disconnect(const ClientContext &client, bool force = false);
    [[nodiscard]] ControllerReply account(const ClientContext &client) const;
    [[nodiscard]] ControllerReply settings(const ClientContext &client) const;
    [[nodiscard]] ControllerReply saveSettings(const ClientContext &client, const QJsonObject &params);
    [[nodiscard]] ControllerReply diagnostics() const;
    [[nodiscard]] StoreResult<Session> requireSession(const ClientContext &client) const;
    [[nodiscard]] QString persistConnectionOwner(const QString &sid, TunnelTechnology technology);
    void clearConnectionOwner();
    void setFailure(QString error);

    mutable QMutex mutex_;
    SecureStore store_;
    NordApiClient api_;
    WireGuardTunnel wireGuardTunnel_;
    OpenVpnTunnel openVpnTunnel_;
    QMutex operationMutex_;
    State state_;
    QString connectionOwnerSid_;
    QString connectionOwnerPath_;
    QHash<QString, QVector<Server>> serverCache_;
};

}
