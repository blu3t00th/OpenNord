#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace opennord {

enum class ConnectionStatus {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Disconnecting,
    Error,
};

enum class TunnelTechnology { NordLynx, OpenVpn };
enum class OpenVpnProtocol { Udp, Tcp };

struct Credentials {
    qint64 id{};
    QString username;
    QString password;
    QString nordLynxPrivateKey;
};

struct User {
    qint64 id{};
    QString email;
    QString username;
};

struct ServerGroup {
    qint64 id{};
    QString title;
};

struct Location {
    qint64 countryId{};
    qint64 cityId{};
    QString country;
    QString countryCode;
    QString city;
    int serverCount{};

    [[nodiscard]] QJsonObject toJson() const;
};

struct Server {
    qint64 id{};
    QString name;
    QString hostname;
    QString station;
    int load{};
    QString status;
    QString country;
    QString countryCode;
    QString city;
    QString publicKey;
    QVector<ServerGroup> groups;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static std::optional<Server> fromNordJson(const QJsonObject &json);
};

struct Settings {
    bool autoConnect{};
    bool launchAtStartup{};
    bool killSwitch{true};
    bool allowLan{};
    QStringList customDns{QStringLiteral("103.86.96.100"), QStringLiteral("103.86.99.100")};
    QString preferredCountry;
    TunnelTechnology technology{TunnelTechnology::NordLynx};
    OpenVpnProtocol openVpnProtocol{OpenVpnProtocol::Udp};

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static Settings fromJson(const QJsonObject &json);
};

struct Session {
    QString token;
    Credentials credentials;
    User user;
    Settings settings;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static std::optional<Session> fromJson(const QJsonObject &json);
};

struct State {
    ConnectionStatus status{ConnectionStatus::Disconnected};
    bool authenticated{};
    bool wireGuardReady{};
    bool openVpnReady{};
    TunnelTechnology technology{TunnelTechnology::NordLynx};
    OpenVpnProtocol openVpnProtocol{OpenVpnProtocol::Udp};
    std::optional<Server> server;
    QDateTime connectedAt;
    QString error;

    [[nodiscard]] QJsonObject toJson() const;
};

[[nodiscard]] QString statusName(ConnectionStatus status);
[[nodiscard]] QString technologyName(TunnelTechnology technology);
[[nodiscard]] QString openVpnProtocolName(OpenVpnProtocol protocol);

}
