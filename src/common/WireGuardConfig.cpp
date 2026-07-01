#include "common/WireGuardConfig.h"

#include <QByteArray>
#include <QHostAddress>
#include <QRegularExpression>

namespace opennord {
namespace {

bool validKey(const QString &key)
{
    const auto decoded = QByteArray::fromBase64(key.trimmed().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    return decoded.size() == 32;
}

}

ConfigResult buildWireGuardConfig(const Credentials &credentials, const Server &server, const Settings &settings)
{
    QHostAddress endpoint;
    if (!endpoint.setAddress(server.station) || endpoint.protocol() != QAbstractSocket::IPv4Protocol) {
        return {{}, QStringLiteral("server endpoint is not a valid IPv4 address")};
    }
    if (!validKey(credentials.nordLynxPrivateKey)) {
        return {{}, QStringLiteral("NordLynx private key is invalid")};
    }
    if (!validKey(server.publicKey)) {
        return {{}, QStringLiteral("server WireGuard public key is invalid")};
    }

    QStringList dns;
    for (const auto &candidate : settings.customDns) {
        QHostAddress address;
        if (!address.setAddress(candidate.trimmed())) {
            return {{}, QStringLiteral("custom DNS address is invalid: %1").arg(candidate)};
        }
        dns.append(address.toString());
    }
    if (dns.isEmpty()) {
        dns = {QStringLiteral("103.86.96.100"), QStringLiteral("103.86.99.100")};
    }

    const auto allowed = settings.killSwitch
        ? QStringLiteral("0.0.0.0/0, ::/0")
        : QStringLiteral("0.0.0.0/1, 128.0.0.0/1, ::/1, 8000::/1");

    return {QStringLiteral(
        "[Interface]\n"
        "PrivateKey = %1\n"
        "Address = 10.5.0.2/16\n"
        "DNS = %2\n\n"
        "[Peer]\n"
        "PublicKey = %3\n"
        "AllowedIPs = %4\n"
        "Endpoint = %5:51820\n"
        "PersistentKeepalive = 25\n")
        .arg(credentials.nordLynxPrivateKey.trimmed(), dns.join(QStringLiteral(", ")), server.publicKey.trimmed(), allowed, server.station), {}};
}

}

