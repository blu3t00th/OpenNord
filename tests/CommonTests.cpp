#include "common/Models.h"
#include "common/OpenVpnConfig.h"
#include "common/Protocol.h"
#include "common/WireGuardConfig.h"

#include <QJsonDocument>
#include <QtTest>

using namespace opennord;

class CommonTests final : public QObject
{
    Q_OBJECT

private slots:
    void protocolRoundTrip();
    void protocolRejectsOversize();
    void wireGuardStrictAndFlexibleRoutes();
    void serverParsingExtractsNordLynxMetadata();
    void openVpnConfigOverridesUntrustedRuntimeDirectives();
    void settingsRoundTripPreservesOpenVpn();
    void openVpnConfigRejectsUnterminatedTrustBlock();
    void openVpnManagementValuesStayOnOneLine();
};

void CommonTests::protocolRoundTrip()
{
    QByteArray buffer = protocol::encodeFrame({{QStringLiteral("id"), 4}, {QStringLiteral("method"), QStringLiteral("status")}});
    QJsonObject decoded;
    QString error;
    QVERIFY(protocol::tryDecodeFrame(buffer, decoded, error));
    QCOMPARE(decoded.value(QStringLiteral("method")).toString(), QStringLiteral("status"));
    QVERIFY(buffer.isEmpty());
}

void CommonTests::protocolRejectsOversize()
{
    QByteArray buffer(4, '\0');
    qToLittleEndian(protocol::MaxFrameSize + 1, buffer.data());
    QJsonObject decoded;
    QString error;
    QVERIFY(!protocol::tryDecodeFrame(buffer, decoded, error));
    QVERIFY(!error.isEmpty());
}

void CommonTests::wireGuardStrictAndFlexibleRoutes()
{
    const auto key = QString::fromLatin1(QByteArray(32, '\0').toBase64());
    Credentials credentials{.nordLynxPrivateKey = key};
    Server server{.station = QStringLiteral("192.0.2.12"), .publicKey = key};
    Settings settings;
    settings.killSwitch = true;
    const auto strict = buildWireGuardConfig(credentials, server, settings);
    QVERIFY(strict.ok());
    QVERIFY(strict.value.contains(QStringLiteral("AllowedIPs = 0.0.0.0/0, ::/0")));
    settings.killSwitch = false;
    const auto flexible = buildWireGuardConfig(credentials, server, settings);
    QVERIFY(flexible.value.contains(QStringLiteral("0.0.0.0/1, 128.0.0.0/1")));
}

void CommonTests::serverParsingExtractsNordLynxMetadata()
{
    const auto json = QJsonDocument::fromJson(R"({
        "id": 1,
        "name": "Sweden #1",
        "hostname": "se1.nordvpn.com",
        "station": "192.0.2.1",
        "load": 12,
        "status": "online",
        "locations": [{"country":{"name":"Sweden","code":"SE","city":{"name":"Stockholm"}}}],
        "technologies": [{"id":35,"metadata":[{"name":"public_key","value":" test-key "}]}]
    })").object();
    const auto server = Server::fromNordJson(json);
    QVERIFY(server.has_value());
    QCOMPARE(server->publicKey, QStringLiteral("test-key"));
    QCOMPARE(server->city, QStringLiteral("Stockholm"));
}

void CommonTests::openVpnConfigOverridesUntrustedRuntimeDirectives()
{
    const QByteArray profile = R"(client
remote old.example 1 udp
auth-user-pass old-file
up dangerous.exe
config C:/Windows/System32/secret.conf
tls-verify verifier.exe
<connection>
remote nested.example 2 udp
up nested-dangerous.exe
</connection>
remote-cert-tls server
<ca>
certificate
</ca>
<tls-auth>
key
</tls-auth>
)";
    const Server server{.hostname = QStringLiteral("se123.nordvpn.com"), .station = QStringLiteral("192.0.2.2")};
    Settings settings;
    settings.openVpnProtocol = OpenVpnProtocol::Tcp;
    settings.customDns = {QStringLiteral("103.86.96.100")};
    const auto config = buildOpenVpnConfig(profile, server, settings, 31194, QStringLiteral("C:/ProgramData/OpenNord/mgmt.key"));
    QVERIFY(config.ok());
    QVERIFY(config.value.contains(QStringLiteral("proto tcp4-client")));
    QVERIFY(config.value.contains(QStringLiteral("remote 192.0.2.2 443 tcp4")));
    QVERIFY(config.value.contains(QStringLiteral("verify-x509-name se123.nordvpn.com name")));
    QVERIFY(config.value.contains(QStringLiteral("management-hold")));
    QVERIFY(!config.value.contains(QStringLiteral("dangerous.exe")));
    QVERIFY(!config.value.contains(QStringLiteral("nested.example")));
    QVERIFY(!config.value.contains(QStringLiteral("secret.conf")));
    QVERIFY(!config.value.contains(QStringLiteral("verifier.exe")));
    QVERIFY(!config.value.contains(QStringLiteral("old-file")));
    QVERIFY(config.value.contains(QStringLiteral("dhcp-option DNS 103.86.96.100")));
}

void CommonTests::settingsRoundTripPreservesOpenVpn()
{
    Settings settings;
    settings.technology = TunnelTechnology::OpenVpn;
    settings.openVpnProtocol = OpenVpnProtocol::Tcp;
    const auto decoded = Settings::fromJson(settings.toJson());
    QCOMPARE(decoded.technology, TunnelTechnology::OpenVpn);
    QCOMPARE(decoded.openVpnProtocol, OpenVpnProtocol::Tcp);
}

void CommonTests::openVpnConfigRejectsUnterminatedTrustBlock()
{
    Settings settings;
    const Server server{.hostname = QStringLiteral("se123.nordvpn.com"), .station = QStringLiteral("192.0.2.2")};
    const auto config = buildOpenVpnConfig("client\n<ca>\ncertificate\n", server, settings, 31194,
                                          QStringLiteral("C:/ProgramData/OpenNord/mgmt.key"));
    QVERIFY(!config.ok());
}

void CommonTests::openVpnManagementValuesStayOnOneLine()
{
    const auto escaped = openVpnManagementEscape(QStringLiteral("user\npassword\\\""));
    QVERIFY(!escaped.contains(u'\n'));
    QCOMPARE(escaped, QStringLiteral("\"user\\npassword\\\\\\\"\""));
}

QTEST_MAIN(CommonTests)
#include "CommonTests.moc"
