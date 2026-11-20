#pragma once

#include <QString>

namespace dv::account {

// The Google "Desktop app" OAuth client of the suite, decrypted from the copy that the build embedded (generated\dv_account\GoogleDesktopOAuth.h).
// The key sits next to the ciphertext in the program: this hides the pair from a casual look at the exe, it is NOT a secret store.
// (Google documents that the secret of a Desktop app client cannot be kept secret.)
struct OAuthClient {
    QString id;
    QString secret;
    bool valid() const { return !id.isEmpty() && !secret.isEmpty(); }
};

OAuthClient embeddedClient();

// Same decryption for a test header (key / iv / ciphertext, all Base64).
OAuthClient decryptClient(const char* key, const char* iv, const char* ciphertext);

} // namespace dv::account
