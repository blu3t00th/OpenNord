#pragma once

#include <QByteArray>
#include <QString>

#include <windows.h>

namespace opennord::windows {

struct SecurityResult {
    QString value;
    QString error;
    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

[[nodiscard]] QString lastErrorMessage(DWORD error = GetLastError());
[[nodiscard]] SecurityResult namedPipeClientSid(HANDLE pipe);
[[nodiscard]] QByteArray protectForMachine(const QByteArray &plain, QString &error);
[[nodiscard]] QByteArray unprotectForMachine(const QByteArray &ciphertext, QString &error);
[[nodiscard]] bool applyPrivateFileAcl(const QString &path, const QString &userSid, bool userCanWrite, QString &error);
[[nodiscard]] SECURITY_ATTRIBUTES pipeSecurityAttributes(PSECURITY_DESCRIPTOR &descriptor, QString &error);

}

