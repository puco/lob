#pragma once

#include <QMap>
#include <QString>
#include <QUrl>

namespace Lob
{

/**
 * A link on its way to a browser, after any redirector has been read.
 *
 * The two URLs differ only when the redirector is one we deliberately leave
 * intact -- a corporate link scanner, whose whole job is to be visited.
 */
struct Link {
    QUrl destination; ///< where the link really goes; rules, memory and the picker use this
    QUrl toOpen;      ///< what the browser is handed
    QString wrapper;  ///< host of the outermost redirector, empty if there was none

    static Link plain(const QUrl &url) { return {url, url, {}}; }
    bool wasWrapped() const { return !wrapper.isEmpty(); }

    /// This link read one step further, where @p inner is what its destination
    /// turned out to be -- a shortener resolved over the network, say. The
    /// outermost redirector is still the one to name, and a link scanner still
    /// the URL to visit.
    Link continuedBy(const Link &inner) const
    {
        Link link = inner;
        if (wasWrapped()) {
            link.wrapper = wrapper;
        }
        if (toOpen != destination) {
            link.toOpen = toOpen;
        }
        return link;
    }
};

/**
 * Reads redirectors that carry their destination inside the URL.
 *
 * A link clicked in Slack arrives as slack.com's redirect, not as the page it
 * points at, so without this every such link asks about slack.com and every
 * memory records slack.com -- which is useless, since the next link through the
 * same redirector goes somewhere else entirely.
 *
 * Nothing here touches the network: a redirector either says where it is going
 * in its own query string or it does not, and the ones that do not are for
 * resolveShortener() to deal with, separately and only when asked.
 */
class RedirectUnwrapper
{
public:
    /**
     * @param extraWrappers user-configured "host[/path]" -> query parameter,
     *        for a redirector the built-in table does not know.
     */
    static Link unwrap(const QUrl &url, const QMap<QString, QString> &extraWrappers = {});

    /// Whether this host's redirects can only be resolved by asking it.
    static bool isShortener(const QUrl &url);
};

} // namespace Lob
