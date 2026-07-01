#include "windows/Security.h"

#include "windows/WinHandle.h"

#include <QScopeGuard>
#include <QVarLengthArray>

#include <sddl.h>
#include <dpapi.h>

#include <cstddef>

namespace opennord::windows {

QString lastErrorMessage(DWORD error)
{
    wchar_t *message{};
    const auto size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t *>(&message), 0, nullptr);
    const auto cleanup = qScopeGuard([&] { if (message) LocalFree(message); });
    return size ? QString::fromWCharArray(message, static_cast<qsizetype>(size)).trimmed()
                : QStringLiteral("Windows error %1").arg(error);
}

SecurityResult namedPipeClientSid(HANDLE pipe)
{
    if (!ImpersonateNamedPipeClient(pipe)) {
        return {{}, QStringLiteral("cannot impersonate pipe client: %1").arg(lastErrorMessage())};
    }
    const auto revert = qScopeGuard([] { RevertToSelf(); });

    HANDLE rawToken{};
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &rawToken)) {
        return {{}, QStringLiteral("cannot open client token: %1").arg(lastErrorMessage())};
    }
    UniqueHandle token(rawToken);
    DWORD size{};
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) {
        return {{}, QStringLiteral("cannot size client identity: %1").arg(lastErrorMessage())};
    }
    QVarLengthArray<std::byte, 256> buffer(static_cast<qsizetype>(size));
    if (!GetTokenInformation(token.get(), TokenUser, buffer.data(), size, &size)) {
        return {{}, QStringLiteral("cannot read client identity: %1").arg(lastErrorMessage())};
    }
    const auto *tokenUser = reinterpret_cast<const TOKEN_USER *>(buffer.constData());
    LPWSTR sidText{};
    if (!ConvertSidToStringSidW(tokenUser->User.Sid, &sidText)) {
        return {{}, QStringLiteral("cannot format client SID: %1").arg(lastErrorMessage())};
    }
    const auto freeSid = qScopeGuard([&] { LocalFree(sidText); });
    return {QString::fromWCharArray(sidText), {}};
}

QByteArray protectForMachine(const QByteArray &plain, QString &error)
{
    DATA_BLOB input{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"OpenNord session", nullptr, nullptr, nullptr, CRYPTPROTECT_LOCAL_MACHINE, &output)) {
        error = QStringLiteral("DPAPI encryption failed: %1").arg(lastErrorMessage());
        return {};
    }
    const auto cleanup = qScopeGuard([&] { LocalFree(output.pbData); });
    error.clear();
    return QByteArray(reinterpret_cast<const char *>(output.pbData), static_cast<qsizetype>(output.cbData));
}

QByteArray unprotectForMachine(const QByteArray &ciphertext, QString &error)
{
    DATA_BLOB input{static_cast<DWORD>(ciphertext.size()), reinterpret_cast<BYTE *>(const_cast<char *>(ciphertext.constData()))};
    DATA_BLOB output{};
    LPWSTR description{};
    if (!CryptUnprotectData(&input, &description, nullptr, nullptr, nullptr, 0, &output)) {
        error = QStringLiteral("DPAPI decryption failed: %1").arg(lastErrorMessage());
        return {};
    }
    const auto cleanup = qScopeGuard([&] {
        LocalFree(output.pbData);
        if (description) LocalFree(description);
    });
    error.clear();
    return QByteArray(reinterpret_cast<const char *>(output.pbData), static_cast<qsizetype>(output.cbData));
}

bool applyPrivateFileAcl(const QString &path, const QString &userSid, bool userCanWrite, QString &error)
{
    const auto userAccess = userCanWrite ? QStringLiteral("FRFW") : QStringLiteral("FR");
    const auto sddl = QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;BA)%1")
        .arg(userSid.isEmpty() ? QString() : QStringLiteral("(A;;%1;;;%2)").arg(userAccess, userSid));
    PSECURITY_DESCRIPTOR descriptor{};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            reinterpret_cast<LPCWSTR>(sddl.utf16()), SDDL_REVISION_1, &descriptor, nullptr)) {
        error = QStringLiteral("cannot create file ACL: %1").arg(lastErrorMessage());
        return false;
    }
    const auto cleanup = qScopeGuard([&] { LocalFree(descriptor); });
    if (!SetFileSecurityW(reinterpret_cast<LPCWSTR>(path.utf16()), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor)) {
        error = QStringLiteral("cannot apply file ACL: %1").arg(lastErrorMessage());
        return false;
    }
    error.clear();
    return true;
}

SECURITY_ATTRIBUTES pipeSecurityAttributes(PSECURITY_DESCRIPTOR &descriptor, QString &error)
{
    // LocalSystem and Administrators receive full access. Interactive users may
    // read/write, but each request is associated with the impersonated user SID.
    constexpr auto sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)";
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, nullptr)) {
        error = QStringLiteral("cannot create named-pipe ACL: %1").arg(lastErrorMessage());
        return attributes;
    }
    attributes.lpSecurityDescriptor = descriptor;
    error.clear();
    return attributes;
}

}
