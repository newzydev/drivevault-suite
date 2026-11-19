#include "StandInHandler.h"

#include "StandIns.h"

#include <QBuffer>
#include <QMultiMap>
#include <QUrl>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

namespace dv::br {

void StandInHandler::registerScheme()
{
    QWebEngineUrlScheme scheme(QByteArrayLiteral("dvres"));
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::CorsEnabled | QWebEngineUrlScheme::ContentSecurityPolicyIgnored);
    QWebEngineUrlScheme::registerScheme(scheme);
}

QUrl StandInHandler::urlFor(const QString& name)
{
    return QUrl(QStringLiteral("dvres:/") + name);
}

void StandInHandler::requestStarted(QWebEngineUrlRequestJob* job)
{
    QString name = job->requestUrl().path();
    if (name.startsWith(QLatin1Char('/')))
        name.remove(0, 1);
    ab::StandIn s;
    if (!ab::standIn(name, &s)) {
        job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        return;
    }
    auto* buffer = new QBuffer(job);
    buffer->setData(s.body);
    buffer->open(QIODevice::ReadOnly);
    QMultiMap<QByteArray, QByteArray> headers;
    headers.insert(QByteArrayLiteral("Access-Control-Allow-Origin"), QByteArrayLiteral("*"));
    job->setAdditionalResponseHeaders(headers);
    job->reply(s.mime, buffer);
}

} // namespace dv::br
