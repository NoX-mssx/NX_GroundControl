#include "SecretProtector.h"

#include <QtCore/QByteArray>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

QString SecretProtector::protect(const QString &secret)
{
    if (secret.isEmpty()) {
        return QString();
    }

    const QByteArray plain = secret.toUtf8();

#ifdef Q_OS_WIN
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()));
    input.cbData = static_cast<DWORD>(plain.size());
    DATA_BLOB output = {};
    if (!CryptProtectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return QString();
    }
    const QByteArray encrypted(reinterpret_cast<const char*>(output.pbData), static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    return QString::fromLatin1(encrypted.toBase64());
#else
    return QString::fromLatin1(plain.toBase64());
#endif
}

QString SecretProtector::unprotect(const QString &stored)
{
    if (stored.isEmpty()) {
        return QString();
    }

    const QByteArray raw = QByteArray::fromBase64(stored.toLatin1());

#ifdef Q_OS_WIN
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(raw.constData()));
    input.cbData = static_cast<DWORD>(raw.size());
    DATA_BLOB output = {};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return QString();
    }
    const QString secret = QString::fromUtf8(reinterpret_cast<const char*>(output.pbData), static_cast<qsizetype>(output.cbData));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return secret;
#else
    return QString::fromUtf8(raw);
#endif
}
