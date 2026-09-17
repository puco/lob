#include "core/ShortenerResolver.h"

#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace Lob;

namespace
{

/// The smallest thing that can redirect. Everything stays on the loopback
/// interface: a test that resolves links must not depend on, or reach, the
/// actual internet.
class RedirectingServer : public QTcpServer
{
public:
    enum Behaviour { Redirect, Answer, Refuse, Silence };
    Behaviour behaviour = Redirect;
    QString target; ///< where Redirect sends the client

    QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path));
    }

protected:
    void incomingConnection(qintptr handle) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(handle);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            const QByteArray request = socket->readAll();
            if (!request.contains("\r\n\r\n")) {
                return;
            }
            const bool isFinal = request.startsWith("HEAD /final");
            switch (isFinal ? Answer : behaviour) {
            case Redirect:
                socket->write("HTTP/1.1 301 Moved Permanently\r\nLocation: " + target.toUtf8()
                              + "\r\nContent-Length: 0\r\n\r\n");
                break;
            case Answer:
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
                break;
            case Refuse:
                socket->write("HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n");
                break;
            case Silence:
                return; // accepted, then nothing: the transfer timeout decides
            }
            socket->disconnectFromHost();
        });
    }
};

} // namespace

class ShortenerResolverTest : public QObject
{
    Q_OBJECT

    RedirectingServer server;

    QUrl resolved(const QUrl &url, int timeoutMs = 5000)
    {
        ShortenerResolver resolver;
        QUrl answer;
        bool done = false;
        resolver.resolve(url, [&answer, &done](const QUrl &destination) {
            answer = destination;
            done = true;
        });
        // The callback is the contract: it always arrives, exactly once.
        QTest::qWait(0);
        const bool arrived = QTest::qWaitFor([&done] { return done; }, timeoutMs);
        return arrived ? answer : QUrl();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(server.listen(QHostAddress::LocalHost));
    }

    void aShortenerIsFollowedToWhereItPoints()
    {
        server.behaviour = RedirectingServer::Redirect;
        server.target = server.url(QStringLiteral("/final")).toString();
        QCOMPARE(resolved(server.url(QStringLiteral("/abc"))), server.url(QStringLiteral("/final")));
    }

    void aLinkThatGoesNowhereIsRoutedAsItArrived()
    {
        const QUrl url = server.url(QStringLiteral("/abc"));

        // No redirect at all, and a shortener that refuses HEAD, both mean the
        // same thing: we learned nothing, so the link is unchanged.
        server.behaviour = RedirectingServer::Answer;
        QCOMPARE(resolved(url), url);
        server.behaviour = RedirectingServer::Refuse;
        QCOMPARE(resolved(url), url);
    }

    void aRedirectToSomethingUnroutableIsIgnored()
    {
        // The result goes on a browser command line like any other URL, so it
        // passes the same check -- a shortener does not get to pick the scheme.
        server.behaviour = RedirectingServer::Redirect;
        server.target = QStringLiteral("file:///etc/passwd");
        const QUrl url = server.url(QStringLiteral("/abc"));
        QCOMPARE(resolved(url), url);
    }

    void aServerThatNeverAnswersDoesNotHoldTheLink()
    {
        server.behaviour = RedirectingServer::Silence;
        const QUrl url = server.url(QStringLiteral("/abc"));
        QElapsedTimer elapsed;
        elapsed.start();
        QCOMPARE(resolved(url, ShortenerResolver::kTimeoutMs * 4), url);
        QVERIFY2(elapsed.elapsed() < ShortenerResolver::kTimeoutMs * 3, qPrintable(QString::number(elapsed.elapsed())));
    }

    void anUnreachableHostIsRoutedAsItArrived()
    {
        const QUrl url(QStringLiteral("http://127.0.0.1:1/abc"));
        QCOMPARE(resolved(url), url);
    }
};

QTEST_GUILESS_MAIN(ShortenerResolverTest)
#include "ShortenerResolverTest.moc"
