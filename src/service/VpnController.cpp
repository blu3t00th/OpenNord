#include "service/VpnController.h"

#include "common/Logging.h"
#include "windows/Security.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>

namespace opennord {
namespace {

ControllerReply failure(QString code, QString error) { return {{}, std::move(code), std::move(error)}; }
ControllerReply success(QJsonValue value = {}) { return {std::move(value), {}, {}}; }

QString loginErrorCode(const QString &apiCode)
{
    if (apiCode == QStringLiteral("token_rejected")) return QStringLiteral("invalid_token");
    if (apiCode == QStringLiteral("network_failure")) return QStringLiteral("network_failure");
    return QStringLiteral("authentication_failed");
}

void logLogin(const QString &message)
{
    logging::write(logging::Target::Service, QStringLiteral("token-validation"), message);
}

}

VpnController::VpnController()
{
    connectionOwnerPath_ = QDir(qEnvironmentVariable("ProgramData", QStringLiteral("C:/ProgramData")))
        .filePath(QStringLiteral("OpenNord/active-owner"));
    state_.wireGuardReady = wireGuardTunnel_.ready();
    state_.openVpnReady = openVpnTunnel_.ready();
    if (wireGuardTunnel_.connected()) {
        state_.status = ConnectionStatus::Connected;
        state_.technology = TunnelTechnology::NordLynx;
        QFile owner(connectionOwnerPath_);
        if (owner.open(QIODevice::ReadOnly)) {
            connectionOwnerSid_ = QString::fromUtf8(owner.readLine()).trimmed();
        }
    } else {
        clearConnectionOwner();
    }
}

StoreResult<Session> VpnController::requireSession(const ClientContext &client) const
{
    const auto result = store_.load(client.sid);
    if (!result.ok()) return {{}, result.error == QStringLiteral("not_authenticated") ? QStringLiteral("sign in first") : result.error};
    return result;
}

ControllerReply VpnController::handle(const ClientContext &client, const QString &method, const QJsonObject &params)
{
    if (method == QStringLiteral("ping")) return success(QStringLiteral("pong"));
    if (method == QStringLiteral("status")) return status(client);
    if (method == QStringLiteral("login")) return login(client, params);
    if (method == QStringLiteral("logout")) return logout(client);
    if (method == QStringLiteral("locations")) return locations(client);
    if (method == QStringLiteral("servers")) return servers(client, params);
    if (method == QStringLiteral("connect")) return connect(client, params, false);
    if (method == QStringLiteral("connectLocation")) return connect(client, params, false);
    if (method == QStringLiteral("quickConnect")) return connect(client, params, true);
    if (method == QStringLiteral("disconnect")) return disconnect(client);
    if (method == QStringLiteral("account")) return account(client);
    if (method == QStringLiteral("settings")) return settings(client);
    if (method == QStringLiteral("saveSettings")) return saveSettings(client, params);
    if (method == QStringLiteral("diagnostics")) return diagnostics();
    return failure(QStringLiteral("unknown_method"), QStringLiteral("unknown RPC method"));
}

ControllerReply VpnController::status(const ClientContext &client) const
{
    auto session = store_.load(client.sid);
    QMutexLocker locker(&mutex_);
    auto state = state_;
    state.authenticated = session.ok();
    state.wireGuardReady = wireGuardTunnel_.ready();
    state.openVpnReady = openVpnTunnel_.ready();
    const auto wireGuardConnected = wireGuardTunnel_.connected();
    const auto openVpnConnected = openVpnTunnel_.connected();
    const auto openVpnRunning = openVpnTunnel_.running();
    if (wireGuardConnected) {
        state.status = ConnectionStatus::Connected;
        state.technology = TunnelTechnology::NordLynx;
    } else if (openVpnConnected) {
        state.status = ConnectionStatus::Connected;
        state.technology = TunnelTechnology::OpenVpn;
    } else if (openVpnRunning) {
        state.status = ConnectionStatus::Reconnecting;
        state.technology = TunnelTechnology::OpenVpn;
    } else if (state.status == ConnectionStatus::Connected || state.status == ConnectionStatus::Reconnecting) {
        state.status = ConnectionStatus::Disconnected;
        state.server.reset();
        state.connectedAt = {};
    }
    if (!wireGuardConnected && !openVpnRunning && session.ok()) {
        state.technology = session.value.settings.technology;
        state.openVpnProtocol = session.value.settings.openVpnProtocol;
    }
    if (!connectionOwnerSid_.isEmpty() && connectionOwnerSid_ != client.sid) {
        state.server.reset();
        state.connectedAt = {};
    }
    return success(state.toJson());
}

ControllerReply VpnController::login(const ClientContext &client, const QJsonObject &params)
{
    logLogin(QStringLiteral("token validation requested"));
    const auto token = params.value(QStringLiteral("token")).toString().trimmed();
    static const QRegularExpression tokenPattern(QStringLiteral("^[a-f0-9]{32,4096}$"));
    if (!tokenPattern.match(token).hasMatch()) {
        logLogin(QStringLiteral("token rejected locally because its format is invalid"));
        return failure(QStringLiteral("invalid_token"), QStringLiteral("access token must contain lowercase hexadecimal characters only"));
    }
    const auto credentials = api_.credentials(token);
    if (!credentials.ok()) {
        logLogin(QStringLiteral("credential validation failed with %1: %2").arg(credentials.code, credentials.error));
        return failure(loginErrorCode(credentials.code), credentials.error);
    }
    const auto user = api_.currentUser(token);
    if (!user.ok()) {
        logLogin(QStringLiteral("account validation failed with %1: %2").arg(user.code, user.error));
        return failure(loginErrorCode(user.code), user.error);
    }
    Session session{token, credentials.value, user.value, {}};
    const auto saveError = store_.save(client.sid, session);
    if (!saveError.isEmpty()) {
        logLogin(QStringLiteral("validated token could not be stored: %1").arg(saveError));
        return failure(QStringLiteral("storage_failed"), saveError);
    }
    logLogin(QStringLiteral("token validation and session storage completed successfully"));
    return success(QJsonObject{{QStringLiteral("id"), user.value.id}, {QStringLiteral("email"), user.value.email}, {QStringLiteral("username"), user.value.username}});
}

ControllerReply VpnController::logout(const ClientContext &client)
{
    bool ownsConnection{};
    {
        QMutexLocker locker(&mutex_);
        ownsConnection = connectionOwnerSid_ == client.sid;
    }
    if (ownsConnection) {
        const auto tunnelReply = disconnect(client);
        if (!tunnelReply.ok()) return tunnelReply;
    }
    const auto error = store_.remove(client.sid);
    if (!error.isEmpty()) return failure(QStringLiteral("storage_failed"), error);
    QMutexLocker locker(&mutex_);
    serverCache_.remove(client.sid);
    return success();
}

ControllerReply VpnController::servers(const ClientContext &client, const QJsonObject &params)
{
    const auto session = requireSession(client);
    if (!session.ok()) return failure(QStringLiteral("not_authenticated"), QStringLiteral("sign in first"));
    const auto result = api_.recommendedServers(session.value.settings, params.value(QStringLiteral("countryCode")).toString());
    if (!result.ok()) return failure(QStringLiteral("server_discovery_failed"), result.error);
    {
        QMutexLocker locker(&mutex_);
        serverCache_.insert(client.sid, result.value);
    }
    QJsonArray array;
    for (const auto &server : result.value) array.append(server.toJson());
    return success(array);
}

ControllerReply VpnController::locations(const ClientContext &client)
{
    const auto session = requireSession(client);
    if (!session.ok()) return failure(QStringLiteral("not_authenticated"), QStringLiteral("sign in first"));
    const auto result = api_.locations();
    if (!result.ok()) return failure(result.code == QStringLiteral("network_failure")
        ? QStringLiteral("network_failure") : QStringLiteral("location_discovery_failed"), result.error);
    QJsonArray array;
    for (const auto &location : result.value) array.append(location.toJson());
    return success(array);
}

ControllerReply VpnController::connect(const ClientContext &client, const QJsonObject &params, bool quick)
{
    QMutexLocker operationLocker(&operationMutex_);
    auto session = requireSession(client);
    if (!session.ok()) return failure(QStringLiteral("not_authenticated"), session.error);
    QVector<Server> available;
    {
        QMutexLocker locker(&mutex_);
        if (state_.status == ConnectionStatus::Connecting || state_.status == ConnectionStatus::Disconnecting) {
            return failure(QStringLiteral("busy"), QStringLiteral("a connection change is already in progress"));
        }
        if ((wireGuardTunnel_.connected() || openVpnTunnel_.running())
            && !connectionOwnerSid_.isEmpty() && connectionOwnerSid_ != client.sid) {
            return failure(QStringLiteral("in_use"), QStringLiteral("another signed-in Windows user owns the active VPN connection"));
        }
        state_.status = ConnectionStatus::Connecting;
        state_.error.clear();
        available = serverCache_.value(client.sid);
    }

    const auto locationRequest = params.contains(QStringLiteral("locationCountryId"));
    if (locationRequest) {
        const auto catalog = api_.locations();
        if (!catalog.ok()) {
            setFailure(catalog.error);
            return failure(catalog.code == QStringLiteral("network_failure")
                ? QStringLiteral("network_failure") : QStringLiteral("location_discovery_failed"), catalog.error);
        }
        const auto requestedCountryId = params.value(QStringLiteral("locationCountryId")).toInteger();
        const auto requestedCityId = params.value(QStringLiteral("locationCityId")).toInteger();
        std::optional<Location> location;
        for (const auto &candidate : catalog.value) {
            if (candidate.countryId == requestedCountryId && candidate.cityId == requestedCityId) {
                location = candidate;
                break;
            }
        }
        if (!location.has_value()) {
            setFailure(QStringLiteral("selected location is no longer available"));
            return failure(QStringLiteral("location_unavailable"), QStringLiteral("selected location is no longer available"));
        }
        const auto discovered = api_.recommendedServers(session.value.settings, *location);
        if (!discovered.ok()) {
            setFailure(discovered.error);
            return failure(discovered.code == QStringLiteral("network_failure")
                ? QStringLiteral("network_failure") : QStringLiteral("server_discovery_failed"), discovered.error);
        }
        available = discovered.value;
        QMutexLocker locker(&mutex_);
        serverCache_.insert(client.sid, available);
    } else if (available.isEmpty() || quick) {
        const auto discovered = api_.recommendedServers(session.value.settings, session.value.settings.preferredCountry);
        if (!discovered.ok()) {
            setFailure(discovered.error);
            return failure(QStringLiteral("server_discovery_failed"), discovered.error);
        }
        available = discovered.value;
        QMutexLocker locker(&mutex_);
        serverCache_.insert(client.sid, available);
    }

    std::optional<Server> selected;
    if (locationRequest) {
        selected = available.first();
    } else if (quick) {
        selected = available.at(QRandomGenerator::global()->bounded(static_cast<int>(available.size())));
    } else {
        const auto requestedId = params.value(QStringLiteral("serverId")).toInteger();
        for (const auto &server : available) if (server.id == requestedId) selected = server;
    }
    if (!selected.has_value()) {
        setFailure(QStringLiteral("selected server is no longer available"));
        return failure(QStringLiteral("server_unavailable"), QStringLiteral("selected server is no longer available"));
    }

    QString error;
    if (session.value.settings.technology == TunnelTechnology::OpenVpn) {
        const auto profile = api_.openVpnConfig(*selected, session.value.settings.openVpnProtocol);
        if (!profile.ok()) {
            setFailure(profile.error);
            return failure(QStringLiteral("configuration_failed"), profile.error);
        }
        if (wireGuardTunnel_.connected()) error = wireGuardTunnel_.disconnect();
        if (error.isEmpty()) {
            error = openVpnTunnel_.connect(session.value.credentials, *selected, session.value.settings, profile.value);
        }
    } else {
        if (openVpnTunnel_.running()) error = openVpnTunnel_.disconnect();
        if (error.isEmpty()) error = wireGuardTunnel_.connect(session.value.credentials, *selected, session.value.settings);
    }
    if (!error.isEmpty()) {
        setFailure(error);
        return failure(QStringLiteral("connection_failed"), error);
    }
    const auto ownerError = persistConnectionOwner(client.sid, session.value.settings.technology);
    if (!ownerError.isEmpty()) {
        if (session.value.settings.technology == TunnelTechnology::OpenVpn) (void)openVpnTunnel_.disconnect();
        else (void)wireGuardTunnel_.disconnect();
        setFailure(ownerError);
        return failure(QStringLiteral("storage_failed"), ownerError);
    }
    QMutexLocker locker(&mutex_);
    state_.status = ConnectionStatus::Connected;
    state_.technology = session.value.settings.technology;
    state_.openVpnProtocol = session.value.settings.openVpnProtocol;
    state_.server = selected;
    connectionOwnerSid_ = client.sid;
    state_.connectedAt = QDateTime::currentDateTimeUtc();
    state_.error.clear();
    return success(state_.toJson());
}

ControllerReply VpnController::disconnect(const ClientContext &client, bool force)
{
    QMutexLocker operationLocker(&operationMutex_);
    {
        QMutexLocker locker(&mutex_);
        if (!force && !connectionOwnerSid_.isEmpty() && connectionOwnerSid_ != client.sid) {
            return failure(QStringLiteral("not_owner"), QStringLiteral("another Windows user owns the active VPN connection"));
        }
        state_.status = ConnectionStatus::Disconnecting;
    }
    QStringList errors;
    const auto openVpnError = openVpnTunnel_.disconnect();
    if (!openVpnError.isEmpty()) errors.append(openVpnError);
    const auto wireGuardError = wireGuardTunnel_.disconnect();
    if (!wireGuardError.isEmpty()) errors.append(wireGuardError);
    if (!errors.isEmpty()) {
        const auto combined = errors.join(QStringLiteral("; "));
        setFailure(combined);
        return failure(QStringLiteral("disconnect_failed"), combined);
    }
    QMutexLocker locker(&mutex_);
    state_.status = ConnectionStatus::Disconnected;
    state_.server.reset();
    state_.connectedAt = {};
    connectionOwnerSid_.clear();
    clearConnectionOwner();
    state_.error.clear();
    return success(state_.toJson());
}

ControllerReply VpnController::account(const ClientContext &client) const
{
    const auto session = requireSession(client);
    if (!session.ok()) return failure(QStringLiteral("not_authenticated"), session.error);
    return success(QJsonObject{{QStringLiteral("id"), session.value.user.id}, {QStringLiteral("email"), session.value.user.email}, {QStringLiteral("username"), session.value.user.username}});
}

ControllerReply VpnController::settings(const ClientContext &client) const
{
    const auto session = requireSession(client);
    if (!session.ok()) return failure(QStringLiteral("not_authenticated"), session.error);
    return success(session.value.settings.toJson());
}

ControllerReply VpnController::saveSettings(const ClientContext &client, const QJsonObject &params)
{
    auto session = requireSession(client);
    if (!session.ok()) return failure(QStringLiteral("not_authenticated"), session.error);
    auto next = Settings::fromJson(params);
    if (!next.preferredCountry.isEmpty() && !QRegularExpression(QStringLiteral("^[A-Z]{2}$")).match(next.preferredCountry).hasMatch()) {
        return failure(QStringLiteral("invalid_settings"), QStringLiteral("preferred country must be a two-letter code"));
    }
    for (const auto &dns : next.customDns) {
        QHostAddress address;
        if (!address.setAddress(dns)) return failure(QStringLiteral("invalid_settings"), QStringLiteral("invalid DNS address: %1").arg(dns));
    }
    if (next.killSwitch) next.allowLan = false;
    session.value.settings = next;
    const auto error = store_.save(client.sid, session.value);
    if (!error.isEmpty()) return failure(QStringLiteral("storage_failed"), error);
    QMutexLocker locker(&mutex_);
    serverCache_.remove(client.sid);
    if (state_.status == ConnectionStatus::Disconnected || state_.status == ConnectionStatus::Error) {
        state_.technology = next.technology;
        state_.openVpnProtocol = next.openVpnProtocol;
    }
    return success(next.toJson());
}

ControllerReply VpnController::diagnostics() const
{
    QMutexLocker locker(&mutex_);
    return success(QJsonObject{
        {QStringLiteral("version"), QStringLiteral(OPENNORD_VERSION)},
        {QStringLiteral("wireGuardPath"), wireGuardTunnel_.executable()},
        {QStringLiteral("wireGuardConfigPath"), wireGuardTunnel_.configPath()},
        {QStringLiteral("openVpnPath"), openVpnTunnel_.executable()},
        {QStringLiteral("sessionDirectory"), store_.directory()},
        {QStringLiteral("lastError"), state_.error},
    });
}

void VpnController::setFailure(QString error)
{
    QMutexLocker locker(&mutex_);
    state_.status = ConnectionStatus::Error;
    state_.server.reset();
    state_.connectedAt = {};
    state_.error = std::move(error);
}

QString VpnController::persistConnectionOwner(const QString &sid, TunnelTechnology technology)
{
    if (!QDir().mkpath(QFileInfo(connectionOwnerPath_).absolutePath())) return QStringLiteral("cannot create service state directory");
    QSaveFile file(connectionOwnerPath_);
    const auto data = sid.toUtf8() + '\n' + technologyName(technology).toUtf8() + '\n';
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        return QStringLiteral("cannot persist active connection owner");
    }
    QString error;
    if (!windows::applyPrivateFileAcl(connectionOwnerPath_, {}, false, error)) return error;
    return {};
}

void VpnController::clearConnectionOwner()
{
    QFile::remove(connectionOwnerPath_);
}

void VpnController::shutdown()
{
    (void)disconnect(ClientContext{}, true);
}

}
