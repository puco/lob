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
};

} // namespace Lob
