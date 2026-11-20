#pragma once

#include <QList>
#include <QString>
#include <QUrl>

// Pure helpers of the browser (no WebEngine): what the address bar does with typed text, how a file name is made safe and unique.
namespace dv::br {

struct SearchEngine {
    QString id;
    QString name;
    QString urlTemplate;      // "%s" is replaced by the percent-encoded words
};

QList<SearchEngine> searchEngines();
QString defaultSearchEngineId();
QUrl searchUrl(const QString& engineId, const QString& words);

// Typed text -> address: a full address stays, "host.tld[:port][/path]" and "localhost[:port]" get a scheme (https, but http for localhost and IPs),
// a Windows path becomes a file: address, everything else is a search with the chosen engine. Empty text gives about:blank.
QUrl resolveInput(const QString& input, const QString& engineId);

// The address as shown when the bar is not being edited: no scheme, no "www.", no trailing "/".
QString prettyUrl(const QString& url);
QString hostOf(const QString& url);

// A Google sign-in page (accounts.google.com ServiceLogin / AccountChooser, https) without an account named gets the signed-in profile's
// e-mail as "Email" and "login_hint", so the page opens with the address filled in. Returns an empty URL when nothing has to change.
QUrl googleSignInUrl(const QUrl& url, const QString& email);

// Downloads
QString safeFileName(const QString& name);                          // Windows-safe, never empty
QString uniqueFileName(const QString& dir, const QString& name);    // "name (1).ext" when "name.ext" exists in dir
bool riskyFileName(const QString& name);                            // exe, msi, bat, ps1, js, lnk ... : the bar warns

} // namespace dv::br
