#include "api/NordApiClient.h"

#include "api/ResponseVerifier.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>

namespace opennord {
namespace {

constexpr auto BaseUrl = "https://api.nordvpn.com";
constexpr auto UserAgent = "OpenNord-Windows/" OPENNORD_VERSION;
constexpr qsizetype MaxResponseSize = 20 * 1024 * 1024;

QString recommendedPath(qint64 countryId, qint64 cityId, qint64 technology)
{
    QUrl url(QString::fromLatin1(BaseUrl) + QStringLiteral("/v1/servers/recommendations"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("20"));
    query.addQueryItem(QStringLiteral("filters[servers.status]"), QStringLiteral("online"));
    query.addQueryItem(QStringLiteral("filters[servers_technologies]"), QString::number(technology));
    query.addQueryItem(QStringLiteral("filters[servers_technologies][pivot][status]"), QStringLiteral("online"));
    if (countryId > 0) query.addQueryItem(QStringLiteral("filters[country_id]"), QString::number(countryId));
    if (cityId > 0) query.addQueryItem(QStringLiteral("filters[country_city_id]"), QString::number(cityId));
    url.setQuery(query);
    return url.toString(QUrl::FullyEncoded).mid(QString::fromLatin1(BaseUrl).size());
}

}

NordApiClient::NordApiClient() = default;

NordApiClient::ReplyData NordApiClient::get(const QString &path, const QString &token) const
{
    return getUrl(QUrl(QString::fromLatin1(BaseUrl) + path), token);
}

NordApiClient::ReplyData NordApiClient::getUrl(const QUrl &url, const QString &token) const
{
    ReplyData result;
    auto *thread = QThread::create([this, &result, url, token] {
        result = getUrlOnQtThread(url, token);
    });
    thread->start();
    thread->wait();
    delete thread;
    return result;
}

NordApiClient::ReplyData NordApiClient::getUrlOnQtThread(const QUrl &url, const QString &token) const
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setRawHeader(QByteArrayLiteral("Accept"), url.host() == QStringLiteral("api.nordvpn.com")
        ? QByteArrayLiteral("application/json") : QByteArrayLiteral("text/plain, application/x-openvpn-profile"));
    request.setRawHeader(QByteArrayLiteral("Accept-Encoding"), QByteArrayLiteral("identity"));
    request.setRawHeader(QByteArrayLiteral("User-Agent"), QByteArray(UserAgent));
    request.setTransferTimeout(30000);
    if (!token.isEmpty()) request.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer token:") + token.toUtf8());

    auto *reply = manager.get(request);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(32000);
    loop.exec();

    ReplyData result;
    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.headers = reply->rawHeaderPairs();
    result.body = reply->read(MaxResponseSize + 1);
    if (result.body.size() > MaxResponseSize) {
        result.errorCode = QStringLiteral("api_failure");
        result.error = QStringLiteral("Nord API response exceeded the size limit");
    } else if (reply->error() != QNetworkReply::NoError && result.status == 0) {
        result.errorCode = QStringLiteral("network_failure");
        result.error = QStringLiteral("Nord API request failed: %1").arg(reply->errorString());
    }
    delete reply;
    if (!result.error.isEmpty()) return result;

    ResponseVerifier verifier;
    if (!verifier.verify(result.status, result.headers, result.body, result.error)) {
        result.errorCode = QStringLiteral("response_verification_failed");
        return result;
    }
    if (result.status < 200 || result.status >= 300) {
        const auto tokenRejected = !token.isEmpty() && (result.status == 401 || result.status == 403);
        result.errorCode = tokenRejected ? QStringLiteral("token_rejected") : QStringLiteral("api_failure");
        result.error = tokenRejected
            ? QStringLiteral("Nord Account token was rejected")
            : QStringLiteral("Nord API returned HTTP %1").arg(result.status);
    }
    return result;
}

ApiResult<Credentials> NordApiClient::credentials(const QString &token) const
{
    const auto reply = get(QStringLiteral("/v1/users/services/credentials"), token);
    if (!reply.error.isEmpty()) return {{}, reply.errorCode, reply.error};
    QJsonParseError parseError;
    const auto object = QJsonDocument::fromJson(reply.body, &parseError).object();
    if (parseError.error != QJsonParseError::NoError) return {{}, QStringLiteral("api_failure"), QStringLiteral("invalid credentials response")};
    Credentials value{
        object.value(QStringLiteral("id")).toInteger(),
        object.value(QStringLiteral("username")).toString(),
        object.value(QStringLiteral("password")).toString(),
        object.value(QStringLiteral("nordlynx_private_key")).toString(),
    };
    if (value.username.isEmpty() || value.password.isEmpty()) {
        return {{}, QStringLiteral("api_failure"), QStringLiteral("Nord API returned no VPN service credentials")};
    }
    return {value, {}, {}};
}

ApiResult<User> NordApiClient::currentUser(const QString &token) const
{
    const auto reply = get(QStringLiteral("/v1/users/current"), token);
    if (!reply.error.isEmpty()) return {{}, reply.errorCode, reply.error};
    QJsonParseError parseError;
    const auto object = QJsonDocument::fromJson(reply.body, &parseError).object();
    if (parseError.error != QJsonParseError::NoError) return {{}, QStringLiteral("api_failure"), QStringLiteral("invalid account response")};
    return {{object.value(QStringLiteral("id")).toInteger(), object.value(QStringLiteral("email")).toString(),
             object.value(QStringLiteral("username")).toString()}, {}, {}};
}

ApiResult<qint64> NordApiClient::countryId(const QString &countryCode) const
{
    if (countryCode.trimmed().isEmpty()) return {0, {}, {}};
    const auto reply = get(QStringLiteral("/v1/servers/countries"));
    if (!reply.error.isEmpty()) return {0, reply.errorCode, reply.error};
    const auto countries = QJsonDocument::fromJson(reply.body).array();
    for (const auto &value : countries) {
        const auto country = value.toObject();
        if (country.value(QStringLiteral("code")).toString().compare(countryCode.trimmed(), Qt::CaseInsensitive) == 0) {
            return {country.value(QStringLiteral("id")).toInteger(), {}, {}};
        }
    }
    return {0, QStringLiteral("api_failure"), QStringLiteral("NordVPN has no country with code %1").arg(countryCode)};
}

ApiResult<QVector<Location>> NordApiClient::locations() const
{
    const auto reply = get(QStringLiteral("/v1/servers/countries"));
    if (!reply.error.isEmpty()) return {{}, reply.errorCode, reply.error};
    QJsonParseError parseError;
    const auto countries = QJsonDocument::fromJson(reply.body, &parseError).array();
    if (parseError.error != QJsonParseError::NoError) {
        return {{}, QStringLiteral("api_failure"), QStringLiteral("invalid locations response")};
    }
    QVector<Location> locations;
    for (const auto &countryValue : countries) {
        const auto country = countryValue.toObject();
        const auto countryId = country.value(QStringLiteral("id")).toInteger();
        const auto countryName = country.value(QStringLiteral("name")).toString();
        const auto countryCode = country.value(QStringLiteral("code")).toString();
        const auto countryServerCount = country.value(QStringLiteral("serverCount")).toInt();
        const auto cities = country.value(QStringLiteral("cities")).toArray();
        if (cities.isEmpty()) {
            locations.append({countryId, 0, countryName, countryCode, {}, countryServerCount});
            continue;
        }
        for (const auto &cityValue : cities) {
            const auto city = cityValue.toObject();
            locations.append({
                countryId,
                city.value(QStringLiteral("id")).toInteger(),
                countryName,
                countryCode,
                city.value(QStringLiteral("name")).toString(),
                city.value(QStringLiteral("serverCount")).toInt(),
            });
        }
    }
    if (locations.isEmpty()) return {{}, QStringLiteral("api_failure"), QStringLiteral("Nord API returned no locations")};
    return {locations, {}, {}};
}

ApiResult<QVector<Server>> NordApiClient::recommendedServers(const Settings &settings, const QString &countryCode) const
{
    const auto country = countryId(countryCode);
    if (!country.ok()) return {{}, country.code, country.error};
    return recommendedServers(settings, country.value, 0);
}

ApiResult<QVector<Server>> NordApiClient::recommendedServers(const Settings &settings, const Location &location) const
{
    return recommendedServers(settings, location.countryId, location.cityId);
}

ApiResult<QVector<Server>> NordApiClient::recommendedServers(const Settings &settings, qint64 countryId, qint64 cityId) const
{
    const auto technology = settings.technology == TunnelTechnology::NordLynx
        ? 35 : settings.openVpnProtocol == OpenVpnProtocol::Tcp ? 5 : 3;
    const auto reply = get(recommendedPath(countryId, cityId, technology));
    if (!reply.error.isEmpty()) return {{}, reply.errorCode, reply.error};
    QJsonParseError parseError;
    const auto array = QJsonDocument::fromJson(reply.body, &parseError).array();
    if (parseError.error != QJsonParseError::NoError) return {{}, QStringLiteral("api_failure"), QStringLiteral("invalid server response")};
    QVector<Server> servers;
    for (const auto &value : array) {
        if (auto server = Server::fromNordJson(value.toObject()); server.has_value()) servers.append(std::move(*server));
    }
    if (servers.isEmpty()) return {{}, QStringLiteral("api_failure"), QStringLiteral("Nord API returned no compatible servers")};
    return {servers, {}, {}};
}

ApiResult<QByteArray> NordApiClient::openVpnConfig(const Server &server, OpenVpnProtocol protocol) const
{
    static const QRegularExpression hostnamePattern(QStringLiteral("^[a-z]{2}[0-9]{1,6}\\.nordvpn\\.com$"));
    if (!hostnamePattern.match(server.hostname).hasMatch()) return {{}, QStringLiteral("configuration_failed"), QStringLiteral("server hostname is invalid")};
    const auto protocolName = openVpnProtocolName(protocol);
    const auto url = QUrl(QStringLiteral("https://downloads.nordcdn.com/configs/files/ovpn_%1/servers/%2.%1.ovpn")
        .arg(protocolName, server.hostname));
    const auto reply = getUrl(url);
    if (!reply.error.isEmpty()) return {{}, reply.errorCode, reply.error};
    if (reply.body.size() > 512 * 1024) return {{}, QStringLiteral("api_failure"), QStringLiteral("OpenVPN profile exceeded the size limit")};
    if (!reply.body.contains("<ca>") || !reply.body.contains("remote-cert-tls server")
        || (!reply.body.contains("<tls-auth>") && !reply.body.contains("<tls-crypt>"))) {
        return {{}, QStringLiteral("response_verification_failed"), QStringLiteral("Nord OpenVPN profile is missing required trust material")};
    }
    return {reply.body, {}, {}};
}

}
