#pragma once

#include <QString>
#include <QUrl>

namespace Lob
{

/// Everything Lob is willing to route. Deliberately narrow: we are registered
/// as a system URL handler, so anything we accept here is something any
/// application on the machine can make us act on.
class UrlSanitizer
{
public:
    /// @param reason filled in with a human-readable explanation on rejection.
    static bool isRoutable(const QUrl &url, QString *reason = nullptr);

    /// Parameters stripped by default. Campaign and click-id tags only --
    /// nothing a page might actually need to render what you asked for.
    static QStringList defaultTrackingParameters();

    /**
     * Removes tracking parameters. A parameter ending in "*" in @p patterns
     * matches by prefix, which is how utm_* is expressed.
     *
     * Only parameters on the curated list are removed, so emptying the query
     * entirely is safe -- and usual, since a newsletter link is frequently
     * nothing but the page plus its campaign tags.
     */
    static QUrl strip(const QUrl &url, const QStringList &patterns);
};

} // namespace Lob
