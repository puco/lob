#include "core/RedirectUnwrapper.h"

#include <QTest>

using namespace Lob;

namespace
{
const QUrl kDestination{QStringLiteral("https://github.com/anthropics/claude-code/issues/42")};

QUrl wrapped(const QString &base, const QString &parameter, const QUrl &target)
{
    return QUrl(base + parameter + QLatin1Char('=')
                + QString::fromUtf8(QUrl::toPercentEncoding(target.toString())));
}
} // namespace

class RedirectUnwrapperTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aChatRedirectIsReadThroughToWhereItGoes()
    {
        // The whole point: every link clicked in Slack arrives on the same
        // host, so without this a memory records slack.com and then applies to
        // every other link from Slack as well.
        for (const auto &base : {"https://slack-redir.net/link?", "https://acme.slack.com/link?",
                                 "https://app.slack.com/link?"}) {
            const Link link = RedirectUnwrapper::unwrap(wrapped(QString::fromLatin1(base), QStringLiteral("url"), kDestination));
            QCOMPARE(link.destination, kDestination);
            QCOMPARE(link.toOpen, kDestination);
            QVERIFY(link.wasWrapped());
            QCOMPARE(link.destination.host(), QStringLiteral("github.com"));
        }
    }

    void aLinkScannerIsReadButStillVisited()
    {
        // Routing has to know where a SafeLinks URL leads. Opening the
        // destination directly would skip the scan the organisation put in the
        // way deliberately, which is not Lob's call to make.
        const QUrl safeLink = wrapped(QStringLiteral("https://eu01.safelinks.protection.outlook.com/?"),
                                      QStringLiteral("url"), kDestination);
        const Link link = RedirectUnwrapper::unwrap(safeLink);
        QCOMPARE(link.destination, kDestination);
        QCOMPARE(link.toOpen, safeLink);
    }

    void aScannerAroundAChatRedirectKeepsTheScanner()
    {
        const QUrl inner = wrapped(QStringLiteral("https://slack-redir.net/link?"), QStringLiteral("url"), kDestination);
        const QUrl outer = wrapped(QStringLiteral("https://eu01.safelinks.protection.outlook.com/?"),
                                   QStringLiteral("url"), inner);
        const Link link = RedirectUnwrapper::unwrap(outer);
        QCOMPARE(link.destination, kDestination);
        QCOMPARE(link.toOpen, outer);
        QCOMPARE(link.wrapper, QStringLiteral("eu01.safelinks.protection.outlook.com"));
    }

    void aChatRedirectAroundAScannerStopsAtTheScanner()
    {
        const QUrl scanner = wrapped(QStringLiteral("https://eu01.safelinks.protection.outlook.com/?"),
                                     QStringLiteral("url"), kDestination);
        const QUrl outer = wrapped(QStringLiteral("https://slack-redir.net/link?"), QStringLiteral("url"), scanner);
        const Link link = RedirectUnwrapper::unwrap(outer);
        QCOMPARE(link.destination, kDestination);
        QCOMPARE(link.toOpen, scanner);
    }

    void onlyTheEndpointThatRedirectsIsTreatedAsOne()
    {
        // A search for a URL, or a Slack permalink, is a destination in its own
        // right. Reading the query of every google.com or slack.com URL as a
        // redirect would route people away from the page they asked for.
        for (const auto &plain : {"https://www.google.com/search?q=https://example.com/",
                                  "https://acme.slack.com/archives/C123/p456",
                                  "https://notslack.com/link?url=https%3A%2F%2Fexample.com%2F"}) {
            const Link link = RedirectUnwrapper::unwrap(QUrl(QString::fromLatin1(plain)));
            QVERIFY2(!link.wasWrapped(), plain);
            QCOMPARE(link.destination, QUrl(QString::fromLatin1(plain)));
        }
    }

    void aRedirectCarryingSomethingUnroutableIsLeftAlone()
    {
        // The destination goes on a command line, so it passes exactly the same
        // check as a URL arriving from anywhere else.
        for (const auto &payload : {"javascript:alert(1)", "file:///etc/passwd",
                                    "https://user:secret@example.com/", "not a url"}) {
            const QUrl url(QStringLiteral("https://slack-redir.net/link?url=")
                           + QString::fromUtf8(QUrl::toPercentEncoding(QString::fromLatin1(payload))));
            const Link link = RedirectUnwrapper::unwrap(url);
            QVERIFY2(!link.wasWrapped(), payload);
            QCOMPARE(link.toOpen, url);
        }
    }

    void aRedirectLoopTerminates()
    {
        QUrl url = kDestination;
        for (int n = 0; n < 12; ++n) {
            url = wrapped(QStringLiteral("https://slack-redir.net/link?"), QStringLiteral("url"), url);
        }
        const Link link = RedirectUnwrapper::unwrap(url);
        QVERIFY(link.wasWrapped());
        // Still a slack-redir URL after the hop limit, and reported as such
        // rather than pretended to be the destination.
        QCOMPARE(link.destination.host(), QStringLiteral("slack-redir.net"));
    }

    void aConfiguredWrapperIsUsedAndOverridesTheBuiltInTable()
    {
        const QUrl url = wrapped(QStringLiteral("https://links.example.internal/go?"), QStringLiteral("to"), kDestination);
        QCOMPARE(RedirectUnwrapper::unwrap(url).destination, url);
        QCOMPARE(RedirectUnwrapper::unwrap(url, {{QStringLiteral("links.example.internal"), QStringLiteral("to")}}).destination,
                 kDestination);

        // A path-scoped entry only applies to that path.
        const QMap<QString, QString> scoped{{QStringLiteral("links.example.internal/go"), QStringLiteral("to")}};
        QCOMPARE(RedirectUnwrapper::unwrap(url, scoped).destination, kDestination);
        const QUrl elsewhere = wrapped(QStringLiteral("https://links.example.internal/other?"), QStringLiteral("to"), kDestination);
        QCOMPARE(RedirectUnwrapper::unwrap(elsewhere, scoped).destination, elsewhere);
    }

    void shortenersAreRecognisedButNotFollowed()
    {
        const QUrl shortened(QStringLiteral("https://t.co/abc123"));
        QVERIFY(RedirectUnwrapper::isShortener(shortened));
        QVERIFY(!RedirectUnwrapper::isShortener(kDestination));

        // Recognising one changes nothing on its own: the destination is not in
        // the URL, and finding it means asking the network.
        const Link link = RedirectUnwrapper::unwrap(shortened);
        QVERIFY(!link.wasWrapped());
        QCOMPARE(link.destination, shortened);
    }
};

QTEST_GUILESS_MAIN(RedirectUnwrapperTest)
#include "RedirectUnwrapperTest.moc"
