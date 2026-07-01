#include "api/ResponseVerifier.h"

#include "windows/Security.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QScopeGuard>

#include <bcrypt.h>
#include <wincrypt.h>

namespace opennord {
namespace {

constexpr auto PublicKeyPem = R"(-----BEGIN PUBLIC KEY-----
MIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAn2cjOkTUpZAOQxVkecLX
ESqu+mXJ7jj0ayIx+38z7bEnwEkwkecbtg7QehtiTKMsdrbBFR+SLhwPrAzjnP3D
1q26mR2jwkKVlGd8E8thH/yCk0X19cwSV3r8H2pjmUbXEA05WjIminL1jIaZcvWT
6/zXqeiZCE0YMJ1V1PnfOue5Z8l6Vj+CL4edPEsiU0NGIkS5Q9UMKElT9meX8S1n
BdhKHjkYXXdqa7dhla2mt+maWXaYQlOwC7iCT0h2838tmbijkKHRdCNFNBhDMfch
UOhkMqaw9wyfM8F+VtTUvDncmA1s8YVBLkbYsKmoAyuCFCuVUtdszUKu/RpHDvUE
Dd7SYXzo2f8U+WqWRUs+GORLVJx66TWVJ3GDoX/MNjla+iOLioRmV+UbN/W6rUyT
w71ddggPinW24U1/sKLeRi+cuDAhHItv+/hH5LaHTbGWWamxQ22Y2OpON4OXK4pM
PLeH4tq6zxtFuR1S1iwOSGZ+tJ5D0yUjmKQZE7cNIioi6Yd0YrgkH2PvMYT+GafC
dLHYcGfvbgySXAYs16j/gdCBvGGkKGf3i/ZS+2dwZbCuLqLamhC3cROI+Mg7d33n
pKHxbTFdLzWRkvUYP2LvmTW9xGNl9gJgLvmravYboa2YlVps1DJcFYjwqbkiFCiZ
Fc1h87dMhVhnBqz8RZXkdqcCAwEAAQ==
-----END PUBLIC KEY-----)";

QByteArray header(const QHash<QByteArray, QByteArray> &headers, const QByteArray &name)
{
    return headers.value(name.toLower());
}

}

ResponseVerifier::ResponseVerifier()
{
    DWORD derSize{};
    if (!CryptStringToBinaryA(PublicKeyPem, 0, CRYPT_STRING_BASE64HEADER, nullptr, &derSize, nullptr, nullptr)) {
        initializationError_ = QStringLiteral("cannot decode embedded response key: %1").arg(windows::lastErrorMessage());
        return;
    }
    QByteArray der(static_cast<qsizetype>(derSize), Qt::Uninitialized);
    if (!CryptStringToBinaryA(PublicKeyPem, 0, CRYPT_STRING_BASE64HEADER,
            reinterpret_cast<BYTE *>(der.data()), &derSize, nullptr, nullptr)) {
        initializationError_ = QStringLiteral("cannot decode embedded response key: %1").arg(windows::lastErrorMessage());
        return;
    }

    CERT_PUBLIC_KEY_INFO *info{};
    DWORD infoSize{};
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO,
            reinterpret_cast<const BYTE *>(der.constData()), derSize,
            CRYPT_DECODE_ALLOC_FLAG, nullptr, &info, &infoSize)) {
        initializationError_ = QStringLiteral("cannot parse embedded response key: %1").arg(windows::lastErrorMessage());
        return;
    }
    const auto cleanup = qScopeGuard([&] { LocalFree(info); });
    BCRYPT_KEY_HANDLE key{};
    if (!CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING, info, 0, nullptr, &key)) {
        initializationError_ = QStringLiteral("cannot import embedded response key: %1").arg(windows::lastErrorMessage());
        return;
    }
    key_ = key;
}

ResponseVerifier::~ResponseVerifier()
{
    if (key_) BCryptDestroyKey(static_cast<BCRYPT_KEY_HANDLE>(key_));
}

bool ResponseVerifier::ready() const { return key_ != nullptr; }

bool ResponseVerifier::verify(int statusCode, const QList<QNetworkReply::RawHeaderPair> &rawHeaders,
                              const QByteArray &body, QString &error) const
{
    if (!ready()) {
        error = initializationError_;
        return false;
    }
    QHash<QByteArray, QByteArray> headers;
    for (const auto &[name, value] : rawHeaders) headers.insert(name.toLower(), value);
    const auto digest = header(headers, QByteArrayLiteral("x-digest"));
    const auto authorization = header(headers, QByteArrayLiteral("x-authorization"));
    const auto acceptBefore = header(headers, QByteArrayLiteral("x-accept-before"));
    const auto signature = QByteArray::fromBase64(header(headers, QByteArrayLiteral("x-signature")), QByteArray::AbortOnBase64DecodingErrors);
    if (digest.isEmpty() || authorization.isEmpty() || acceptBefore.isEmpty() || signature.isEmpty()) {
        error = QStringLiteral("signed response headers are incomplete");
        return false;
    }
    if (!authorization.contains("algorithm=\"rsa-sha256\"") || !authorization.contains("key-id=\"rsa-key-1\"")) {
        error = QStringLiteral("response uses an unsupported signature");
        return false;
    }
    bool validTimestamp{};
    const auto expires = acceptBefore.toLongLong(&validTimestamp);
    if (!validTimestamp || QDateTime::currentSecsSinceEpoch() > expires) {
        error = QStringLiteral("response signature has expired");
        return false;
    }
    const auto actualDigest = QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex();
    const auto emptyDigest = QCryptographicHash::hash({}, QCryptographicHash::Sha256).toHex();
    if (digest != actualDigest && ((statusCode >= 200 && statusCode < 300) || digest != emptyDigest)) {
        error = QStringLiteral("response digest does not match its body");
        return false;
    }
    const auto signedHash = QCryptographicHash::hash(acceptBefore + digest, QCryptographicHash::Sha256);
    BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
    const auto status = BCryptVerifySignature(
        static_cast<BCRYPT_KEY_HANDLE>(key_), &padding,
        reinterpret_cast<PUCHAR>(const_cast<char *>(signedHash.constData())), static_cast<ULONG>(signedHash.size()),
        reinterpret_cast<PUCHAR>(const_cast<char *>(signature.constData())), static_cast<ULONG>(signature.size()),
        BCRYPT_PAD_PKCS1);
    if (!BCRYPT_SUCCESS(status)) {
        error = QStringLiteral("response RSA signature is invalid");
        return false;
    }
    error.clear();
    return true;
}

}
