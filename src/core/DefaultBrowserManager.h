#pragma once

#include <QString>
#include <QStringList>

namespace Lob
{

/**
 * Claims and relinquishes the system http/https handler.
 *
 * Only those two schemes. Not text/html -- that is "open a local HTML file",
 * where the user wants a browser, not a router. Not mailto, not PDF.
 */
class DefaultBrowserManager
{
public:
    static QStringList handledMimeTypes();

    static bool isDefault();

    /// Storage ids currently registered for http and https, for restoring later.
    static QStringList currentHandlers();

    /// Records the present handlers, then registers us. Refuses to record
    /// ourselves, so taking over twice cannot destroy the restore target.
    static bool claim(QString *error = nullptr);

    /// Puts back whatever was recorded by claim().
    static bool restore(QString *error = nullptr);

    /**
     * ~/.config/kde-mimeapps.list outranks ~/.config/mimeapps.list in the
     * spec's precedence chain, so if it sets an http/https default our
     * registration is silently shadowed. Returns the shadowing entry, if any.
     */
    static QString shadowingConfig();
};

} // namespace Lob
