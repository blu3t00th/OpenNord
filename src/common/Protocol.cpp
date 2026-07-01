#include "common/Protocol.h"

#include <QJsonDocument>
#include <QtEndian>

namespace opennord::protocol {

QByteArray encodeFrame(const QJsonObject &message)
{
    const auto payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QByteArray frame(static_cast<qsizetype>(sizeof(quint32)), Qt::Uninitialized);
    qToLittleEndian(static_cast<quint32>(payload.size()), frame.data());
    frame.append(payload);
    return frame;
}

bool tryDecodeFrame(QByteArray &buffer, QJsonObject &message, QString &error)
{
    if (buffer.size() < static_cast<qsizetype>(sizeof(quint32))) {
        return false;
    }
    const auto length = qFromLittleEndian<quint32>(buffer.constData());
    if (length == 0 || length > MaxFrameSize) {
        error = QStringLiteral("invalid RPC frame length");
        buffer.clear();
        return false;
    }
    const auto frameLength = static_cast<qsizetype>(sizeof(quint32) + length);
    if (buffer.size() < frameLength) {
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(buffer.mid(sizeof(quint32), length), &parseError);
    buffer.remove(0, frameLength);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("invalid RPC JSON payload");
        return false;
    }
    message = document.object();
    error.clear();
    return true;
}

QJsonObject success(qint64 id, const QJsonValue &result)
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("ok"), true}, {QStringLiteral("result"), result}};
}

QJsonObject failure(qint64 id, QString code, QString message)
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), std::move(code)}, {QStringLiteral("message"), std::move(message)}}},
    };
}

}

