#pragma once

#include "common/Models.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QString>
#include <QUrl>
#include <QVector>

namespace opennord {

template<typename T>
struct ApiResult {
    T value{};
    QString code;
    QString error;
    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

class NordApiClient final
{
public:
    NordApiClient();

    [[nodiscard]] ApiResult<Credentials> credentials(const QString &token) const;
    [[nodiscard]] ApiResult<User> currentUser(const QString &token) const;
    [[nodiscard]] ApiResult<QVector<Location>> locations() const;
    [[nodiscard]] ApiResult<QVector<Server>> recommendedServers(const Settings &settings, const QString &countryCode = {}) const;
    [[nodiscard]] ApiResult<QVector<Server>> recommendedServers(const Settings &settings, const Location &location) const;
    [[nodiscard]] ApiResult<QByteArray> openVpnConfig(const Server &server, OpenVpnProtocol protocol) const;

private:
    struct ReplyData {
        int status{};
        QByteArray body;
        QList<QNetworkReply::RawHeaderPair> headers;
        QString errorCode;
        QString error;
    };

    [[nodiscard]] ReplyData get(const QString &path, const QString &token = {}) const;
    [[nodiscard]] ReplyData getUrl(const QUrl &url, const QString &token = {}) const;
    [[nodiscard]] ReplyData getUrlOnQtThread(const QUrl &url, const QString &token) const;
    [[nodiscard]] ApiResult<qint64> countryId(const QString &countryCode) const;
    [[nodiscard]] ApiResult<QVector<Server>> recommendedServers(const Settings &settings, qint64 countryId, qint64 cityId) const;
};

}
