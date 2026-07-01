#include "common/Models.h"

#include <QJsonValue>

namespace opennord {

QJsonObject Location::toJson() const
{
    return {
        {QStringLiteral("countryId"), countryId},
        {QStringLiteral("cityId"), cityId},
        {QStringLiteral("country"), country},
        {QStringLiteral("countryCode"), countryCode},
        {QStringLiteral("city"), city},
        {QStringLiteral("serverCount"), serverCount},
    };
}

QJsonObject Server::toJson() const
{
    QJsonArray groupArray;
    for (const auto &group : groups) {
        groupArray.append(QJsonObject{{QStringLiteral("id"), group.id}, {QStringLiteral("title"), group.title}});
    }
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("hostname"), hostname},
        {QStringLiteral("station"), station},
        {QStringLiteral("load"), load},
        {QStringLiteral("status"), status},
        {QStringLiteral("country"), country},
        {QStringLiteral("countryCode"), countryCode},
        {QStringLiteral("city"), city},
        {QStringLiteral("groups"), groupArray},
    };
}

std::optional<Server> Server::fromNordJson(const QJsonObject &json)
{
    Server server;
    server.id = json.value(QStringLiteral("id")).toInteger();
    server.name = json.value(QStringLiteral("name")).toString();
    server.hostname = json.value(QStringLiteral("hostname")).toString();
    server.station = json.value(QStringLiteral("station")).toString();
    server.load = json.value(QStringLiteral("load")).toInt();
    server.status = json.value(QStringLiteral("status")).toString();

    const auto locations = json.value(QStringLiteral("locations")).toArray();
    if (!locations.isEmpty()) {
        const auto location = locations.first().toObject();
        const auto country = location.value(QStringLiteral("country")).toObject();
        server.country = country.value(QStringLiteral("name")).toString();
        server.countryCode = country.value(QStringLiteral("code")).toString();
        server.city = location.value(QStringLiteral("city")).toObject().value(QStringLiteral("name")).toString();
        if (server.city.isEmpty()) {
            server.city = country.value(QStringLiteral("city")).toObject().value(QStringLiteral("name")).toString();
        }
    }

    const auto technologies = json.value(QStringLiteral("technologies")).toArray();
    for (const auto &technologyValue : technologies) {
        const auto technology = technologyValue.toObject();
        if (technology.value(QStringLiteral("id")).toInt() != 35) {
            continue;
        }
        for (const auto &metadataValue : technology.value(QStringLiteral("metadata")).toArray()) {
            const auto metadata = metadataValue.toObject();
            if (metadata.value(QStringLiteral("name")).toString() == QStringLiteral("public_key")) {
                server.publicKey = metadata.value(QStringLiteral("value")).toString().trimmed();
            }
        }
    }

    for (const auto &groupValue : json.value(QStringLiteral("groups")).toArray()) {
        const auto group = groupValue.toObject();
        server.groups.append({group.value(QStringLiteral("id")).toInteger(), group.value(QStringLiteral("title")).toString()});
    }

    if (server.id <= 0 || server.station.isEmpty() || server.hostname.isEmpty()) {
        return std::nullopt;
    }
    return server;
}

QJsonObject Settings::toJson() const
{
    QJsonArray dns;
    for (const auto &address : customDns) {
        dns.append(address);
    }
    return {
        {QStringLiteral("autoConnect"), autoConnect},
        {QStringLiteral("launchAtStartup"), launchAtStartup},
        {QStringLiteral("killSwitch"), killSwitch},
        {QStringLiteral("allowLan"), allowLan},
        {QStringLiteral("customDns"), dns},
        {QStringLiteral("preferredCountry"), preferredCountry},
        {QStringLiteral("technology"), technologyName(technology)},
        {QStringLiteral("openVpnProtocol"), openVpnProtocolName(openVpnProtocol)},
    };
}

Settings Settings::fromJson(const QJsonObject &json)
{
    Settings settings;
    settings.autoConnect = json.value(QStringLiteral("autoConnect")).toBool();
    settings.launchAtStartup = json.value(QStringLiteral("launchAtStartup")).toBool();
    settings.killSwitch = json.value(QStringLiteral("killSwitch")).toBool(true);
    settings.allowLan = json.value(QStringLiteral("allowLan")).toBool();
    settings.preferredCountry = json.value(QStringLiteral("preferredCountry")).toString().trimmed().toUpper();
    settings.technology = json.value(QStringLiteral("technology")).toString() == QStringLiteral("openvpn")
        ? TunnelTechnology::OpenVpn : TunnelTechnology::NordLynx;
    settings.openVpnProtocol = json.value(QStringLiteral("openVpnProtocol")).toString() == QStringLiteral("tcp")
        ? OpenVpnProtocol::Tcp : OpenVpnProtocol::Udp;
    const auto dns = json.value(QStringLiteral("customDns")).toArray();
    if (!dns.isEmpty()) {
        settings.customDns.clear();
        for (const auto &value : dns) {
            settings.customDns.append(value.toString());
        }
    }
    return settings;
}

QJsonObject Session::toJson() const
{
    return {
        {QStringLiteral("token"), token},
        {QStringLiteral("credentials"), QJsonObject{
            {QStringLiteral("id"), credentials.id},
            {QStringLiteral("username"), credentials.username},
            {QStringLiteral("password"), credentials.password},
            {QStringLiteral("nordLynxPrivateKey"), credentials.nordLynxPrivateKey},
        }},
        {QStringLiteral("user"), QJsonObject{
            {QStringLiteral("id"), user.id},
            {QStringLiteral("email"), user.email},
            {QStringLiteral("username"), user.username},
        }},
        {QStringLiteral("settings"), settings.toJson()},
    };
}

std::optional<Session> Session::fromJson(const QJsonObject &json)
{
    Session session;
    session.token = json.value(QStringLiteral("token")).toString();
    const auto credentials = json.value(QStringLiteral("credentials")).toObject();
    session.credentials.id = credentials.value(QStringLiteral("id")).toInteger();
    session.credentials.username = credentials.value(QStringLiteral("username")).toString();
    session.credentials.password = credentials.value(QStringLiteral("password")).toString();
    session.credentials.nordLynxPrivateKey = credentials.value(QStringLiteral("nordLynxPrivateKey")).toString();
    const auto user = json.value(QStringLiteral("user")).toObject();
    session.user.id = user.value(QStringLiteral("id")).toInteger();
    session.user.email = user.value(QStringLiteral("email")).toString();
    session.user.username = user.value(QStringLiteral("username")).toString();
    session.settings = Settings::fromJson(json.value(QStringLiteral("settings")).toObject());
    if (session.token.isEmpty() || session.credentials.username.isEmpty() || session.credentials.password.isEmpty()) {
        return std::nullopt;
    }
    return session;
}

QJsonObject State::toJson() const
{
    QJsonObject json{
        {QStringLiteral("status"), statusName(status)},
        {QStringLiteral("authenticated"), authenticated},
        {QStringLiteral("wireGuardReady"), wireGuardReady},
        {QStringLiteral("openVpnReady"), openVpnReady},
        {QStringLiteral("technology"), technologyName(technology)},
        {QStringLiteral("openVpnProtocol"), openVpnProtocolName(openVpnProtocol)},
        {QStringLiteral("error"), error},
    };
    if (server.has_value()) {
        json.insert(QStringLiteral("server"), server->toJson());
    }
    if (connectedAt.isValid()) {
        json.insert(QStringLiteral("connectedAt"), connectedAt.toUTC().toString(Qt::ISODateWithMs));
    }
    return json;
}

QString statusName(ConnectionStatus status)
{
    switch (status) {
    case ConnectionStatus::Disconnected: return QStringLiteral("disconnected");
    case ConnectionStatus::Connecting: return QStringLiteral("connecting");
    case ConnectionStatus::Connected: return QStringLiteral("connected");
    case ConnectionStatus::Reconnecting: return QStringLiteral("reconnecting");
    case ConnectionStatus::Disconnecting: return QStringLiteral("disconnecting");
    case ConnectionStatus::Error: return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

QString technologyName(TunnelTechnology technology)
{
    return technology == TunnelTechnology::OpenVpn ? QStringLiteral("openvpn") : QStringLiteral("nordlynx");
}

QString openVpnProtocolName(OpenVpnProtocol protocol)
{
    return protocol == OpenVpnProtocol::Tcp ? QStringLiteral("tcp") : QStringLiteral("udp");
}

}
