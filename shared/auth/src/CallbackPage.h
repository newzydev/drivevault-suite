#pragma once

#include <QByteArray>

namespace dv::account {

// The page the browser shows when Google sends the person back to the program (http://127.0.0.1:<port>/oauth/callback).
// Self-contained (no network, no external font or picture; its CSP allows only inline style and script), Thai + English, in the DriveVault
// look (dark neutral surface, the accent colours of the three programs). ok = the sign-in came back with a code; otherwise it was cancelled or
// failed and the page says so plainly.
QByteArray callbackPage(bool ok);

} // namespace dv::account
