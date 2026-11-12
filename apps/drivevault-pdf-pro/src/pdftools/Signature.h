#pragma once

#include <QString>
#include <QStringList>

namespace dp::tools {

struct SignOptions {
    QString pfxPath;              // the certificate with its private key (.pfx / .p12)
    QString pfxPassword;
    QString name;                 // shown in the signature (default: the name in the certificate)
    QString reason;
    QString location;
};

struct SignResult {
    bool ok = false;
    // "" | "file" | "format" | "password" (the PDF needs a password) | "encrypted" | "alreadySigned" | "exists" | "write" | "badPfx" | "wrongPfxPassword" | "noKey" | "tooLarge" | "unknown"
    QString error;
    QString detail;
    QString signer;               // the name that was signed with
};

// Writes a copy of `inFile` with an invisible digital signature (PKCS#7 detached, SHA-256, the certificate chain of the signer is not included).
// The signature covers the whole file. Not supported (refused with a clear error): protected files and files that are already signed (rewriting the file
// would break the earlier signature). The source is never changed.
SignResult signPdf(const QString& inFile, const QString& outFile, const SignOptions& options, bool overwrite);

struct SignatureInfo {
    QString fieldName;
    QString signer;               // name in the certificate of the signer
    QString reason;
    QString location;
    QString signingTime;          // as the signer wrote it (not proof of time)
    bool intact = false;          // the signature matches the signed bytes: they were not changed
    bool coversWholeFile = false; // false: something was added after signing
    // "trusted" (chain ends in a root Windows trusts) | "untrusted" (self-signed or unknown root) | "expired" | "revoked" | "unknown"
    QString trust;
    QString error;                // "" | "badSignature" | "noSigner" | "unreadable"
};

// All signatures of a file with their checks. `error` (when not null): "" | "file" | "format" | "password".
QList<SignatureInfo> verifySignatures(const QString& file, const QString& password, QString* error = nullptr);

} // namespace dp::tools
