#pragma once

#include <QObject>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

namespace Lob
{

/**
 * Asks a shortener where one of its links goes.
 *
 * This is the one redirect that cannot be read locally: t.co keeps the
 * destination on its own server. Finding it means making the request the
 * browser would have made -- before the browser makes it, from this machine,
 * and while the link waits -- which is why nothing calls this unless the
 * configuration asks for it.
 *
 * The request carries no cookies, no credentials and no referrer. It is a HEAD,
 * so the page itself is never fetched, and it gives up quickly: a link that
 * takes too long to resolve is routed as it arrived rather than held up.
 */
class ShortenerResolver : public QObject
{
    Q_OBJECT

public:
    explicit ShortenerResolver(QObject *parent = nullptr);

    /// Calls @p done with where @p url leads, or with @p url itself if that
    /// cannot be established. Always called, exactly once.
    using Resolved = std::function<void(const QUrl &)>;
    virtual void resolve(const QUrl &url, Resolved done);

    /// Long enough for a redirect, short enough not to read as a hang.
    static constexpr int kTimeoutMs = 1500;

private:
    QNetworkAccessManager *m_network;
};

} // namespace Lob
