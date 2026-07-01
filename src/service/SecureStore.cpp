#include "service/SecureStore.h"

#include "windows/Security.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

namespace opennord {

SecureStore::SecureStore()
{
    directory_ = QDir(qEnvironmentVariable("ProgramData", QStringLiteral("C:/ProgramData")))
        .filePath(QStringLiteral("OpenNord/Sessions"));
    QDir().mkpath(directory_);
}

QString SecureStore::pathFor(const QString &sid) const
{
    const auto name = QString::fromLatin1(QCryptographicHash::hash(sid.toUtf8(), QCryptographicHash::Sha256).toHex());
    return QDir(directory_).filePath(name + QStringLiteral(".session"));
}

StoreResult<Session> SecureStore::load(const QString &sid) const
{
    QFile file(pathFor(sid));
    if (!file.exists()) return {{}, QStringLiteral("not_authenticated")};
    if (!file.open(QIODevice::ReadOnly)) return {{}, QStringLiteral("cannot read encrypted session")};
    QString error;
    const auto plain = windows::unprotectForMachine(file.readAll(), error);
    if (!error.isEmpty()) return {{}, error};
    QJsonParseError parseError;
    const auto json = QJsonDocument::fromJson(plain, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        return {{}, QStringLiteral("encrypted session is corrupt")};
    }
    auto session = Session::fromJson(json.object());
    if (!session.has_value()) return {{}, QStringLiteral("encrypted session is incomplete")};
    return {std::move(*session), {}};
}

QString SecureStore::save(const QString &sid, const Session &session) const
{
    QString error;
    const auto encrypted = windows::protectForMachine(QJsonDocument(session.toJson()).toJson(QJsonDocument::Compact), error);
    if (!error.isEmpty()) return error;
    QSaveFile file(pathFor(sid));
    if (!file.open(QIODevice::WriteOnly) || file.write(encrypted) != encrypted.size() || !file.commit()) {
        return QStringLiteral("cannot write encrypted session");
    }
    if (!windows::applyPrivateFileAcl(pathFor(sid), {}, false, error)) return error;
    return {};
}

QString SecureStore::remove(const QString &sid) const
{
    const auto path = pathFor(sid);
    if (!QFile::exists(path) || QFile::remove(path)) return {};
    return QStringLiteral("cannot remove encrypted session");
}

}
