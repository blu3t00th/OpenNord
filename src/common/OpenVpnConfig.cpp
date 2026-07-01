#include "common/OpenVpnConfig.h"

#include <QDir>
#include <QHostAddress>
#include <QRegularExpression>
#include <QSet>

namespace opennord {

QString openVpnManagementEscape(const QString &value)
{
    auto escaped = value;
    escaped.replace(u'\\', QStringLiteral("\\\\"));
    escaped.replace(u'"', QStringLiteral("\\\""));
    escaped.replace(u'\r', QStringLiteral("\\r"));
    escaped.replace(u'\n', QStringLiteral("\\n"));
    return u'"' + escaped + u'"';
}

ConfigResult buildOpenVpnConfig(const QByteArray &signedProfile, const Server &server,
                                const Settings &settings, quint16 managementPort,
                                const QString &managementPasswordPath)
{
    QHostAddress endpoint;
    if (!endpoint.setAddress(server.station) || endpoint.protocol() != QAbstractSocket::IPv4Protocol) {
        return {{}, QStringLiteral("OpenVPN endpoint is not a valid IPv4 address")};
    }
    static const QRegularExpression hostnamePattern(QStringLiteral("^[a-z]{2}[0-9]{1,6}\\.nordvpn\\.com$"));
    if (!hostnamePattern.match(server.hostname).hasMatch()) return {{}, QStringLiteral("OpenVPN server hostname is invalid")};
    if (managementPort == 0) return {{}, QStringLiteral("OpenVPN management port is invalid")};
    if (managementPasswordPath.isEmpty()) return {{}, QStringLiteral("OpenVPN management password path is missing")};

    static const QSet<QString> overridden{
        QStringLiteral("remote"), QStringLiteral("proto"), QStringLiteral("auth-user-pass"),
        QStringLiteral("management"), QStringLiteral("management-query-passwords"), QStringLiteral("management-hold"),
        QStringLiteral("management-client"), QStringLiteral("management-client-auth"),
        QStringLiteral("management-external-cert"), QStringLiteral("management-external-key"),
        QStringLiteral("management-query-proxy"), QStringLiteral("management-query-remote"),
        QStringLiteral("management-signal"), QStringLiteral("management-up-down"),
        QStringLiteral("auth-retry"), QStringLiteral("auth-nocache"), QStringLiteral("verify-x509-name"),
        QStringLiteral("remote-cert-tls"),
        QStringLiteral("dhcp-option"),
        QStringLiteral("block-outside-dns"), QStringLiteral("block-ipv6"), QStringLiteral("disable-dco"),
        QStringLiteral("script-security"), QStringLiteral("up"), QStringLiteral("down"),
        QStringLiteral("route-up"), QStringLiteral("route-pre-down"), QStringLiteral("ipchange"),
        QStringLiteral("tls-verify"), QStringLiteral("auth-user-pass-verify"),
        QStringLiteral("client-connect"), QStringLiteral("client-disconnect"), QStringLiteral("learn-address"),
        QStringLiteral("plugin"), QStringLiteral("daemon"), QStringLiteral("log"),
        QStringLiteral("log-append"), QStringLiteral("status"), QStringLiteral("writepid"),
        QStringLiteral("config"), QStringLiteral("cd"), QStringLiteral("chroot"), QStringLiteral("tmp-dir"),
        QStringLiteral("user"), QStringLiteral("group"), QStringLiteral("askpass"),
        QStringLiteral("ca"), QStringLiteral("capath"), QStringLiteral("cert"), QStringLiteral("extra-certs"),
        QStringLiteral("key"), QStringLiteral("pkcs12"), QStringLiteral("cryptoapicert"),
        QStringLiteral("tls-auth"), QStringLiteral("tls-crypt"), QStringLiteral("tls-crypt-v2"),
        QStringLiteral("http-proxy-user-pass"), QStringLiteral("socks-proxy"),
        QStringLiteral("pull-filter"), QStringLiteral("route-nopull"), QStringLiteral("route-noexec"),
    };

    QStringList output;
    QString inlineBlock;
    static const QSet<QString> allowedInlineBlocks{
        QStringLiteral("ca"), QStringLiteral("cert"), QStringLiteral("key"),
        QStringLiteral("tls-auth"), QStringLiteral("tls-crypt"), QStringLiteral("tls-crypt-v2"),
    };
    static const QRegularExpression openingTag(QStringLiteral("^<([a-z0-9-]+)>$"));
    const auto lines = QString::fromUtf8(signedProfile).replace(QStringLiteral("\r\n"), QStringLiteral("\n")).split(u'\n');
    for (const auto &line : lines) {
        const auto trimmed = line.trimmed();
        if (!inlineBlock.isEmpty()) {
            output.append(line);
            if (trimmed.compare(QStringLiteral("</%1>").arg(inlineBlock), Qt::CaseInsensitive) == 0) inlineBlock.clear();
            continue;
        }
        const auto tagMatch = openingTag.match(trimmed.toLower());
        if (tagMatch.hasMatch() && allowedInlineBlocks.contains(tagMatch.captured(1))) {
            inlineBlock = tagMatch.captured(1);
            output.append(line);
            continue;
        }
        if (trimmed.startsWith(u'<')) continue;
        const auto directive = trimmed.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0).toLower();
        if (!overridden.contains(directive)) output.append(line);
    }
    if (!inlineBlock.isEmpty()) return {{}, QStringLiteral("OpenVPN profile contains an unterminated inline block")};

    auto passwordPath = QDir::fromNativeSeparators(managementPasswordPath);
    passwordPath.replace(u'"', QStringLiteral("\\\""));
    const auto transport = settings.openVpnProtocol == OpenVpnProtocol::Tcp ? QStringLiteral("tcp4-client") : QStringLiteral("udp4");
    const auto remoteTransport = settings.openVpnProtocol == OpenVpnProtocol::Tcp ? QStringLiteral("tcp4") : QStringLiteral("udp4");
    const auto port = settings.openVpnProtocol == OpenVpnProtocol::Tcp ? 443 : 1194;
    output.append({
        QStringLiteral("proto %1").arg(transport),
        QStringLiteral("remote %1 %2 %3").arg(server.station).arg(port).arg(remoteTransport),
        QStringLiteral("auth-user-pass"),
        QStringLiteral("auth-retry nointeract"),
        QStringLiteral("auth-nocache"),
        QStringLiteral("management 127.0.0.1 %1 \"%2\"").arg(managementPort).arg(passwordPath),
        QStringLiteral("management-query-passwords"),
        QStringLiteral("management-hold"),
        QStringLiteral("verify-x509-name %1 name").arg(server.hostname),
        QStringLiteral("remote-cert-tls server"),
        QStringLiteral("block-outside-dns"),
        QStringLiteral("block-ipv6"),
        QStringLiteral("script-security 1"),
        QStringLiteral("connect-retry 5 5"),
        QStringLiteral("disable-dco"),
        QStringLiteral("verb 3"),
    });
    for (const auto &dns : settings.customDns) {
        output.append(QStringLiteral("dhcp-option DNS %1").arg(dns));
    }
    return {output.join(u'\n') + u'\n', {}};
}

}
