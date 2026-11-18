#pragma once

#include <QByteArray>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QtGlobal>

namespace dm::ai {

// When a request to an ONLINE model fails for a reason that goes away by itself (the connection dropped, the service is busy or restarting, a limit per minute was
// reached) it is asked again - after a pause that grows, with a little randomness so that many programs do not come back at the same moment, and at least as long
// as the service asked for (Retry-After). Pure functions, tested in tst_ai. The models of this PC never use this (a failed local server is started again by LocalAgent).
struct RetryPolicy {
    static constexpr int kMaxRetries = 8;                       // tries after the first one
    static constexpr int kBaseMs = 1000;                        // the first pause
    static constexpr int kCapMs = 30000;                        // the longest pause between two tries
    static constexpr int kRetryAfterCapMs = 60000;              // a Retry-After longer than this is not waited for (the user is told instead)
    static constexpr int kTimeoutMs = 120000;                   // no data for two minutes: the request is given up and asked again
    static inline double delayScale = 1.0;                      // tests only: 0.005 makes the pauses 200 times shorter

    // A status that means "try again later", not "this request is wrong": request timeout, too early, too many requests, a server error, a gateway error and the
    // "service overloaded / unavailable" codes of proxies (Cloudflare 520 - 524, Anthropic-style 529).
    static bool httpRetryable(int status)
    {
        return status == 408 || status == 425 || status == 429 || status == 500 || status == 502 || status == 503 || status == 504 || (status >= 520 && status <= 524) || status == 529;
    }

    // A failure of the connection itself.
    static bool networkRetryable(QNetworkReply::NetworkError e)
    {
        switch (e) {
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::RemoteHostClosedError:
        case QNetworkReply::HostNotFoundError:
        case QNetworkReply::TimeoutError:
        case QNetworkReply::OperationCanceledError:             // (the transfer timeout; a cancel by the user is checked before)
        case QNetworkReply::SslHandshakeFailedError:
        case QNetworkReply::TemporaryNetworkFailureError:
        case QNetworkReply::NetworkSessionFailedError:
        case QNetworkReply::BackgroundRequestNotAllowedError:
        case QNetworkReply::ProxyConnectionRefusedError:
        case QNetworkReply::ProxyConnectionClosedError:
        case QNetworkReply::ProxyNotFoundError:
        case QNetworkReply::ProxyTimeoutError:
        case QNetworkReply::UnknownNetworkError:
        case QNetworkReply::UnknownProxyError:
        case QNetworkReply::ProtocolFailure:
        case QNetworkReply::InternalServerError:
        case QNetworkReply::ServiceUnavailableError:
        case QNetworkReply::UnknownServerError:
            return true;
        default:
            return false;                                       // wrong key, not found, forbidden ...: asking again does not help
        }
    }

    // Retry-After in milliseconds ("120" seconds; a date is not understood and gives 0), at most kRetryAfterCapMs.
    static int retryAfterMs(const QByteArray& header)
    {
        bool ok = false;
        const double seconds = header.trimmed().toDouble(&ok);
        if (!ok || seconds <= 0)
            return 0;
        return int(qMin<double>(seconds * 1000.0, kRetryAfterCapMs));
    }

    // The pause before try number `retry` (1 = the first retry): 1 s, 2 s, 4 s, 8 s, 16 s, 30 s ... with -25 % .. +25 % randomness, and not shorter than the
    // service asked for. `random01` (0 .. 1) is a parameter so that tests are exact.
    static int delayMs(int retry, int retryAfter, double random01)
    {
        qint64 base = kBaseMs;
        for (int i = 1; i < retry && base < kCapMs; ++i)
            base *= 2;
        base = qMin<qint64>(base, kCapMs);
        const int jittered = int(double(base) * (0.75 + 0.5 * qBound(0.0, random01, 1.0)));
        return qMax(5, int(double(qMax(jittered, qMin(retryAfter, kRetryAfterCapMs))) * delayScale));
    }
    static int delayMs(int retry, int retryAfter) { return delayMs(retry, retryAfter, QRandomGenerator::global()->generateDouble()); }
};

} // namespace dm::ai
