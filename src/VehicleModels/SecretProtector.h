#pragma once

#include <QtCore/QString>

/// Encrypts small secrets (camera passwords, tunnel keys) before they are written to disk.
/// On Windows the data is bound to the current user account through DPAPI, so the stored file is useless
/// on another machine or account. Other platforms have no equivalent wired up and store plain text.
namespace SecretProtector
{
/// @return base64 text safe to store in JSON, empty on failure or for an empty secret
QString protect(const QString &secret);
/// @return the original secret, empty if the stored value cannot be decrypted
QString unprotect(const QString &stored);
}
