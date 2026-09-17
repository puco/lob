#include "RedirectUnwrapper.h"

#include "UrlSanitizer.h"

#include <QUrlQuery>

namespace Lob
{

namespace
{

/// What to hand the browser once a redirector has been read.
enum class Policy {
    Rewrite,   ///< open the destination; the redirector only existed to count the click
    KeepIntact ///< open the redirector itself; visiting it is the point
};

struct Wrapper {
    QString host;           ///< exact host, or ".example.com" for any subdomain of it
    QString path;           ///< path prefix, or empty for any path
    QStringList parameters; ///< query parameters to read, in order of preference
    Policy policy = Policy::Rewrite;
};

/// Redirectors that state their destination in the URL.
///
/// Link scanners are marked KeepIntact: rewriting a SafeLinks URL would route
/// the link correctly and skip the scan the organisation put in the way on
/// purpose. Lob reads where it goes; it does not decide to go around it.
const QList<Wrapper> &knownWrappers()
{
    static const QList<Wrapper> wrappers = {
        {QStringLiteral("slack-redir.net"), {}, {QStringLiteral("url")}, Policy::Rewrite},
        {QStringLiteral(".slack.com"), QStringLiteral("/link"), {QStringLiteral("url")}, Policy::Rewrite},
        {QStringLiteral("out.reddit.com"), {}, {QStringLiteral("url")}, Policy::Rewrite},
        {QStringLiteral("l.facebook.com"), {}, {QStringLiteral("u")}, Policy::Rewrite},
        {QStringLiteral("lm.facebook.com"), {}, {QStringLiteral("u")}, Policy::Rewrite},
        {QStringLiteral("l.messenger.com"), {}, {QStringLiteral("u")}, Policy::Rewrite},
        {QStringLiteral("l.instagram.com"), {}, {QStringLiteral("u")}, Policy::Rewrite},
        {QStringLiteral("away.vk.com"), QStringLiteral("/away.php"), {QStringLiteral("to")}, Policy::Rewrite},
        {QStringLiteral("t.umblr.com"), QStringLiteral("/redirect"), {QStringLiteral("z")}, Policy::Rewrite},
        {QStringLiteral("steamcommunity.com"), QStringLiteral("/linkfilter"), {QStringLiteral("url")}, Policy::Rewrite},
        {QStringLiteral(".youtube.com"), QStringLiteral("/redirect"), {QStringLiteral("q")}, Policy::Rewrite},
        {QStringLiteral(".linkedin.com"), QStringLiteral("/redir/redirect"), {QStringLiteral("url")}, Policy::Rewrite},
        {QStringLiteral("href.li"), {}, {QString()}, Policy::Rewrite},

        // Search engines wrap outbound results. Only the /url endpoint, so a
        // search whose query happens to contain a URL is left alone.
        {QStringLiteral(".google.com"), QStringLiteral("/url"), {QStringLiteral("q"), QStringLiteral("url")}, Policy::Rewrite},
        {QStringLiteral(".duckduckgo.com"), QStringLiteral("/l"), {QStringLiteral("uddg")}, Policy::Rewrite},

        {QStringLiteral(".safelinks.protection.outlook.com"), {}, {QStringLiteral("url")}, Policy::KeepIntact},
        {QStringLiteral(".safelinks.protection.office365.us"), {}, {QStringLiteral("url")}, Policy::KeepIntact},
    };
    return wrappers;
}

/// Hosts that answer "where does this go?" only over the network.
const QStringList &shortenerHosts()
{
    static const QStringList hosts = {
        QStringLiteral("t.co"),        QStringLiteral("bit.ly"),     QStringLiteral("lnkd.in"),
        QStringLiteral("tinyurl.com"), QStringLiteral("ow.ly"),      QStringLiteral("buff.ly"),
        QStringLiteral("is.gd"),       QStringLiteral("rb.gy"),      QStringLiteral("cutt.ly"),
        QStringLiteral("trib.al"),     QStringLiteral("dlvr.it"),    QStringLiteral("amzn.to"),
        QStringLiteral("goo.gl"),      QStringLiteral("shorturl.at"), QStringLiteral("t.ly"),
    };
    return hosts;
}

bool hostMatches(const QString &host, const QString &pattern)
{
    if (!pattern.startsWith(QLatin1Char('.'))) {
        return host == pattern;
    }
    // ".slack.com" covers app.slack.com and slack.com itself, but never
    // notslack.com -- the leading dot is a label boundary, not a substring.
    return host.endsWith(pattern) || host == QStringView(pattern).mid(1);
}

const Wrapper *wrapperFor(const QUrl &url, const QList<Wrapper> &wrappers)
{
    const QString host = url.host().toLower();
    for (const Wrapper &wrapper : wrappers) {
        if (!hostMatches(host, wrapper.host)) {
            continue;
        }
        if (!wrapper.path.isEmpty() && !url.path().startsWith(wrapper.path)) {
            continue;
        }
        return &wrapper;
    }
    return nullptr;
}

/// The URL a redirector is carrying, or an empty URL if it is not carrying one.
QUrl destinationIn(const QUrl &url, const Wrapper &wrapper)
{
    const QUrlQuery query(url);
    for (const QString &parameter : wrapper.parameters) {
        // An empty name means the query itself is the URL, as href.li does it.
        const QString value = parameter.isEmpty() ? url.query(QUrl::FullyDecoded)
                                                  : query.queryItemValue(parameter, QUrl::FullyDecoded);
        if (value.isEmpty()) {
            continue;
        }
        const QUrl destination(value, QUrl::StrictMode);
        if (destination.isValid() && !destination.scheme().isEmpty()) {
            return destination;
        }
    }
    return {};
}

QList<Wrapper> allWrappers(const QMap<QString, QString> &extra)
{
    QList<Wrapper> wrappers;
    // Configured entries are tried first, so a redirector the built-in table
    // reads wrongly can be corrected without waiting for a release.
    for (auto it = extra.cbegin(); it != extra.cend(); ++it) {
        const QString location = it.key();
        const int slash = location.indexOf(QLatin1Char('/'));
        wrappers.append({slash < 0 ? location : location.left(slash),
                         slash < 0 ? QString() : location.mid(slash),
                         {it.value()},
                         Policy::Rewrite});
    }
    wrappers.append(knownWrappers());
    return wrappers;
}

/// One redirector wrapping another is ordinary (a scanner around a Slack link).
/// A chain longer than this is not a link, it is a loop or an attempt at one.
constexpr int kMaxHops = 5;

} // namespace

Link RedirectUnwrapper::unwrap(const QUrl &url, const QMap<QString, QString> &extraWrappers)
{
    const QList<Wrapper> wrappers = allWrappers(extraWrappers);

    Link link = Link::plain(url);
    QUrl current = url;
    bool openPinned = false;

    for (int hop = 0; hop < kMaxHops; ++hop) {
        const Wrapper *wrapper = wrapperFor(current, wrappers);
        if (!wrapper) {
            break;
        }

        const QUrl destination = destinationIn(current, *wrapper);
        // A redirector carrying something we would refuse to route, or itself,
        // is left exactly as it arrived rather than half-read.
        if (destination == current || !UrlSanitizer::isRoutable(destination)) {
            break;
        }

        if (link.wrapper.isEmpty()) {
            link.wrapper = current.host();
        }
        if (wrapper->policy == Policy::KeepIntact) {
            openPinned = true; // this layer is the one that must be visited
        } else if (!openPinned) {
            link.toOpen = destination;
        }
        current = destination;
    }

    link.destination = current;
    return link;
}

bool RedirectUnwrapper::isShortener(const QUrl &url)
{
    return shortenerHosts().contains(url.host().toLower());
}

} // namespace Lob
