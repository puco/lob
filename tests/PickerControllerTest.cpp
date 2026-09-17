#include "ui/PickerController.h"
#include "core/RuleStore.h"
#include "core/RuleEngine.h"
#include <QTemporaryDir>
#include <QFile>
#include <QTest>
#include <QSignalSpy>

using namespace Lob;

class FakeLauncher : public Launcher
{
public:
    QList<Completion> callbacks;
    QList<QSharedPointer<LaunchOperation>> operations;
    QList<QUrl> urls;
    void launch(const Target &, const QUrl &url, bool, QWindow *, Completion done,
                const QString &, QSharedPointer<LaunchOperation> operation) override
    {
        urls.append(url);
        callbacks.append(done);
        operations.append(operation);
    }
};

class PickerControllerTest : public QObject
{
    Q_OBJECT
    QTemporaryDir config;
    const QUrl url{QStringLiteral("https://example.com/")};
    void populate(PickerController &picker)
    {
        Target target;
        target.id = QStringLiteral("test.desktop#profile");
        target.storageId = QStringLiteral("test.desktop");
        target.profileKey = QStringLiteral("profile");
        target.label = QStringLiteral("Test");
        picker.setTargets({target});
    }
private Q_SLOTS:
    void initTestCase() { QVERIFY(config.isValid()); qputenv("XDG_CONFIG_HOME", config.path().toUtf8()); }
    void init() { QFile::remove(RuleStore::filePath()); }

    void repeatedSelectionCompletesOnceAndRemembersAfterSuccess()
    {
        RuleStore store;
        FakeLauncher launcher;
        PickerController picker(&store, nullptr, &launcher);
        populate(picker);
        QSignalSpy finished(&picker, &PickerController::finished);
        picker.showPicker(Link::plain(url), {});
        picker.choose(0, false, true);
        picker.choose(0, false, true);
        QCOMPARE(launcher.callbacks.size(), 1);
        QVERIFY(!store.hasMemory(url.host()));
        QCOMPARE(finished.count(), 0);
        launcher.callbacks[0]({});
        launcher.callbacks[0]({});
        QCOMPARE(finished.count(), 1);
        QVERIFY(store.hasMemory(url.host()));
    }

    void failureKeepsUrlForRetry()
    {
        RuleStore store;
        FakeLauncher launcher;
        PickerController picker(&store, nullptr, &launcher);
        populate(picker);
        QSignalSpy finished(&picker, &PickerController::finished);
        picker.showPicker(Link::plain(url), {});
        picker.choose(0, false, true);
        launcher.callbacks[0]({LaunchResult::Failed, QStringLiteral("failed")});
        QCOMPARE(picker.mode(), PickerController::Mode::Picker);
        QCOMPARE(picker.url(), url.toString());
        QVERIFY(!picker.errorMessage().isEmpty());
        QVERIFY(!store.hasMemory(url.host()));
        QCOMPARE(finished.count(), 0);
        picker.choose(0, false, false);
        launcher.callbacks[0]({}); // a late completion from the failed attempt
        QCOMPARE(finished.count(), 0);
        launcher.callbacks[1]({});
        QCOMPARE(finished.count(), 1);
    }

    void cancelledCallbackCannotFinishNextRequest()
    {
        RuleStore store;
        FakeLauncher launcher;
        PickerController picker(&store, nullptr, &launcher);
        populate(picker);
        QSignalSpy finished(&picker, &PickerController::finished);
        picker.showPicker(Link::plain(url), {});
        picker.choose(0, false, false);
        picker.cancel();
        QVERIFY(launcher.operations[0]->cancelled);
        QCOMPARE(finished.count(), 1);
        picker.showPicker(Link::plain(QUrl(QStringLiteral("https://next.example/"))), {});
        launcher.callbacks[0]({LaunchResult::Cancelled, {}});
        QCOMPARE(finished.count(), 1);
        QCOMPARE(picker.mode(), PickerController::Mode::Picker);
        picker.choose(0, false, false);
        launcher.operations[1]->dispatched = true;
        picker.cancel();
        QCOMPARE(finished.count(), 1);
        launcher.callbacks[1]({});
        QCOMPARE(finished.count(), 2);
    }

    void missingTargetWithZeroHoldAsks()
    {
        RuleStore store;
        QVERIFY(store.setHoldMs(0));
        FakeLauncher launcher;
        PickerController picker(&store, nullptr, &launcher);
        Decision decision;
        decision.action = RuleAction::Open;
        decision.targetId = QStringLiteral("missing");
        QSignalSpy finished(&picker, &PickerController::finished);
        picker.showHold(Link::plain(url), {}, decision);
        QCOMPARE(picker.mode(), PickerController::Mode::Picker);
        QCOMPARE(finished.count(), 0);
    }

    void newDefaultBrowserMemoryIsNotTreatedAsAmbiguousLegacyProfile()
    {
        RuleStore store;
        QVERIFY(store.setHoldMs(0));
        QVERIFY(store.remember(url.host(), QStringLiteral("test.desktop"), false));
        FakeLauncher launcher;
        PickerController picker(&store, nullptr, &launcher);
        Target base;
        base.id = base.storageId = QStringLiteral("test.desktop");
        Target first = base;
        first.id += QStringLiteral("#one");
        first.profileKey = QStringLiteral("one");
        Target second = base;
        second.id += QStringLiteral("#two");
        second.profileKey = QStringLiteral("two");
        picker.setTargets({base, first, second});
        picker.showHold(Link::plain(url), {}, RuleEngine::decide(url, store.rules()));
        QCOMPARE(launcher.callbacks.size(), 1);
        QCOMPARE(picker.mode(), PickerController::Mode::Launching);
    }

    void interruptStopsHoldAndWatchdogBelongsToCurrentRequest()
    {
        RuleStore store;
        QVERIFY(store.setHoldMs(20));
        FakeLauncher launcher;
        PickerController picker(&store, nullptr, &launcher, 300);
        populate(picker);
        Decision decision;
        decision.action = RuleAction::Open;
        decision.targetId = QStringLiteral("test.desktop#profile");
        picker.showHold(Link::plain(url), {}, decision);
        picker.interruptHold();
        QTest::qWait(50);
        QVERIFY(launcher.callbacks.isEmpty());
        picker.cancel();
        QTest::qWait(150);
        QSignalSpy finished(&picker, &PickerController::finished);
        picker.showPicker(Link::plain(url), {});
        QTest::qWait(150); // the previous request's watchdog would have fired
        QCOMPARE(finished.count(), 0);
        QTRY_COMPARE(finished.count(), 1);
    }

    void watchdogDoesNotAdvanceQueueWhileDispatchIsPending()
    {
        RuleStore store;
        FakeLauncher launcher;
        PickerController picker(&store, nullptr, &launcher, 30);
        populate(picker);
        QSignalSpy finished(&picker, &PickerController::finished);
        picker.showPicker(Link::plain(url), {});
        picker.choose(0, false, false);
        launcher.operations[0]->dispatched = true;
        QTest::qWait(60);
        QCOMPARE(picker.mode(), PickerController::Mode::Launching);
        QCOMPARE(finished.count(), 0);
        launcher.callbacks[0]({});
        QCOMPARE(finished.count(), 1);
    }

    void aScannedLinkOpensTheScannerAndRemembersWhereItGoes()
    {
        // The browser gets the URL that has to be visited; the question asked,
        // and the answer written down, are about where that link leads.
        RuleStore store;
        FakeLauncher launcher;
        PickerController picker(&store, nullptr, &launcher);
        populate(picker);
        const QUrl scanner(QStringLiteral("https://eu01.safelinks.protection.outlook.com/?url=x"));
        const Link link{QUrl(QStringLiteral("https://github.com/anthropics")), scanner,
                        QStringLiteral("eu01.safelinks.protection.outlook.com")};

        picker.showPicker(link, {});
        QCOMPARE(picker.displayHost(), QStringLiteral("github.com"));
        QCOMPARE(picker.wrapperHost(), QStringLiteral("eu01.safelinks.protection.outlook.com"));

        picker.choose(0, false, true);
        QCOMPARE(launcher.urls.size(), 1);
        QCOMPARE(launcher.urls.constFirst(), scanner);
        launcher.callbacks[0]({});
        QVERIFY(store.hasMemory(QStringLiteral("github.com")));
        QVERIFY(!store.hasMemory(QStringLiteral("eu01.safelinks.protection.outlook.com")));
    }

    void aDispatchThatNeverReportsBackIsEventuallyGivenUpOn()
    {
        // The first expiry drops the input grab and keeps waiting. A second one
        // means no answer is coming, and every later link is queued behind it.
        RuleStore store;
        FakeLauncher launcher;
        PickerController picker(&store, nullptr, &launcher, 30);
        populate(picker);
        QSignalSpy finished(&picker, &PickerController::finished);
        QSignalSpy errors(&picker, &PickerController::errorOccurred);
        picker.showPicker(Link::plain(url), {});
        picker.choose(0, false, false);
        launcher.operations[0]->dispatched = true;

        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(errors.count(), 1);
        QCOMPARE(picker.mode(), PickerController::Mode::Idle);

        // The answer arriving afterwards belongs to a request that is over.
        launcher.callbacks[0]({});
        QCOMPARE(finished.count(), 1);
    }
};
QTEST_GUILESS_MAIN(PickerControllerTest)
#include "PickerControllerTest.moc"
