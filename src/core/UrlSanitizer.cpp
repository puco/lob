#include "UrlSanitizer.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QUrlQuery>

namespace Lob
{

bool UrlSanitizer::isRoutable(const QUrl &url, QString *reason)
{
    const auto fail = [reason](const QString &message) {
        if (reason) {
            *reason = message;
        }
        return false;
    };

    if (!url.isValid() || url.isEmpty()) {
        return fail(i18n("Not a valid URL."));
    }

    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        return fail(i18n("Only http and https URLs are routed (got '%1').", url.scheme()));
    }

    if (url.host().isEmpty()) {
        return fail(i18n("URL has no host."));
    }

    // Credentials in a URL handed to us by an arbitrary application are either
    // a mistake or an attempt to get them into a browser's history, and we
    // would be passing them on a command line either way.
    if (!url.userInfo().isEmpty()) {
        return fail(i18n("URL contains embedded credentials."));
    }

    return true;
}


QStringList UrlSanitizer::defaultTrackingParameters()
{
    return {
        QStringLiteral("utm_*"),      // Google Analytics campaign tags
        QStringLiteral("fbclid"),     // Facebook
        QStringLiteral("gclid"),      // Google Ads
        QStringLiteral("dclid"),      // DoubleClick
        QStringLiteral("gbraid"),     QStringLiteral("wbraid"), // Google app/web click ids
        QStringLiteral("msclkid"),    // Microsoft Ads
        QStringLiteral("igshid"),     // Instagram
        QStringLiteral("twclid"),     // Twitter/X
        QStringLiteral("ttclid"),     // TikTok
        QStringLiteral("mc_eid"),     QStringLiteral("mc_cid"), // Mailchimp
        QStringLiteral("_hsenc"),     QStringLiteral("_hsmi"),  // HubSpot
        QStringLiteral("vero_id"),    QStringLiteral("vero_conv"),
        QStringLiteral("oly_anon_id"), QStringLiteral("oly_enc_id"),
        QStringLiteral("icid"),       QStringLiteral("scid"),
    };
}

QUrl UrlSanitizer::strip(const QUrl &url, const QStringList &patterns)
{
    if (!url.hasQuery() || patterns.isEmpty()) {
        return url;
    }

    const QUrlQuery query(url);
    const auto items = query.queryItems(QUrl::FullyEncoded);
    if (items.isEmpty()) {
        return url;
    }

    const auto isTracking = [&patterns](const QString &key) {
        for (const QString &pattern : patterns) {
            if (pattern.endsWith(QLatin1Char('*'))) {
                if (key.startsWith(QStringView(pattern).chopped(1), Qt::CaseInsensitive)) {
                    return true;
                }
            } else if (key.compare(pattern, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    };

    QUrlQuery kept;
    for (const auto &item : items) {
        if (!isTracking(item.first)) {
            kept.addQueryItem(item.first, item.second);
        }
    }

    if (kept.queryItems().size() == items.size()) {
        return url;
    }

    // A query left empty is fine and is in fact the common case: a newsletter
    // link is often nothing but the page plus utm_* tags. Only parameters from
    // the curated campaign and click-id list are ever removed, and no site
    // routes on those, so there is nothing here worth preserving.
    QUrl cleaned = url;
    if (kept.isEmpty()) {
        cleaned.setQuery(QString());
    } else {
        cleaned.setQuery(kept);
    }
    return cleaned;
}

} // namespace Lob
