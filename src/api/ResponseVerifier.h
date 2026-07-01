#pragma once

#include <QByteArray>
#include <QNetworkReply>
#include <QString>

namespace opennord {

class ResponseVerifier final
{
public:
    ResponseVerifier();
    ~ResponseVerifier();
    ResponseVerifier(const ResponseVerifier &) = delete;
    ResponseVerifier &operator=(const ResponseVerifier &) = delete;

    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool verify(int statusCode, const QList<QNetworkReply::RawHeaderPair> &headers,
                              const QByteArray &body, QString &error) const;

private:
    void *key_{};
    QString initializationError_;
};

}

