#include "UrlSanitizer.h"

#include <QCoreApplication>

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
        return fail(QCoreApplication::translate("UrlSanitizer", "Not a valid URL."));
    }

    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        return fail(QCoreApplication::translate("UrlSanitizer", "Only http and https URLs are routed (got '%1').")
                        .arg(url.scheme()));
    }

    if (url.host().isEmpty()) {
        return fail(QCoreApplication::translate("UrlSanitizer", "URL has no host."));
    }

    // Credentials in a URL handed to us by an arbitrary application are either
    // a mistake or an attempt to get them into a browser's history, and we
    // would be passing them on a command line either way.
    if (!url.userInfo().isEmpty()) {
        return fail(QCoreApplication::translate("UrlSanitizer", "URL contains embedded credentials."));
    }

    return true;
}

} // namespace Lob
