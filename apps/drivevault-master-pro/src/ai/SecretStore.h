#pragma once

#include <QString>

namespace dm::ai {

// An API key is kept encrypted with the Windows account of this user (DPAPI): the settings file holds only the encrypted text, and only this user on this
// PC can read it back. It is never written in clear text and never logged.
namespace SecretStore {

QString protect(const QString& plain);        // "" when it cannot be encrypted (or `plain` is empty)
QString unprotect(const QString& stored);     // "" when it is not valid for this user

} // namespace SecretStore

} // namespace dm::ai
