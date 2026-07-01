#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace opennord::protocol {

inline constexpr quint32 MaxFrameSize = 1024U * 1024U;
inline constexpr auto PipeName = LR"(\\.\pipe\OpenNord.Service.v1)";
inline constexpr auto ServiceName = L"OpenNordService";

[[nodiscard]] QByteArray encodeFrame(const QJsonObject &message);
[[nodiscard]] bool tryDecodeFrame(QByteArray &buffer, QJsonObject &message, QString &error);
[[nodiscard]] QJsonObject success(qint64 id, const QJsonValue &result);
[[nodiscard]] QJsonObject failure(qint64 id, QString code, QString message);

}
