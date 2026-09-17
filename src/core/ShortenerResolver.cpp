#include "ShortenerResolver.h"

#include "UrlSanitizer.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace Lob
{

namespace
{
/// A shortener pointing at a shortener happens; a dozen of them is a loop.
constexpr int kMaxRedirects = 5;
} // namespace

ShortenerResolver::ShortenerResolver(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    // Nothing here should look like the user's browser: no cookie jar, and no
    // cache that would keep a record of where they were about to go.
    m_network->setCookieJar(nullptr);
    m_network->setAutoDeleteReplies(false);
}

void ShortenerResolver::resolve(const QUrl &url, Resolved done)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(kMaxRedirects);
    request.setTransferTimeout(kTimeoutMs);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("lob/" LOB_VERSION));

    QNetworkReply *reply = m_network->head(request);
    connect(reply, &QNetworkReply::finished, this, [reply, url, done = std::move(done)] {
        const QUrl destination = reply->url();
        const bool usable = reply->error() == QNetworkReply::NoError && destination != url
            && UrlSanitizer::isRoutable(destination);
        reply->deleteLater();

        // Anything else -- an error, a timeout, a shortener that refuses HEAD,
        // a redirect to something we would not route -- means the link goes on
        // exactly as it arrived. It is still a link; we just do not know more.
        done(usable ? destination : url);
    });
}

} // namespace Lob
