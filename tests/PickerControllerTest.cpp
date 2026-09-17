#include "ui/PickerController.h"
#include "ui/TargetModel.h"
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

    /// Three browsers with distinguishable labels, profiles and desktop ids,
    /// so a filter test can tell which field it matched on.
    void populateMany(PickerController &picker)
    {
        auto make = [](const QString &id, const QString &label, const QString &profile) {
            Target t;
            t.id = id;
            t.storageId = id.section(QLatin1Char('#'), 0, 0);
            t.label = label;
            t.profileName = profile;
            t.kind = TargetKind::Browser;
            return t;
        };
        picker.setTargets({
            make(QStringLiteral("firefox.desktop#work"), QStringLiteral("Firefox"), QStringLiteral("Work")),
            make(QStringLiteral("firefox.desktop#home"), QStringLiteral("Firefox"), QStringLiteral("Home")),
            make(QStringLiteral("chromium.desktop"), QStringLiteral("Chromium"), QString()),
        });
    }

    void filteringNarrowsTheListAndRenumbersTheDigits()
    {
        RuleStore store;
        FakeLauncher launcher;
        PickerController picker(&store, nullptr, &launcher);
        populateMany(picker);
        QCOMPARE(picker.targetsModel()->rowCount(), 3);

        picker.setFilter(QStringLiteral("chrom"));
        QCOMPARE(picker.targetsModel()->rowCount(), 1);

        // The digit is the row on screen: after filtering, 1 must mean the one
        // cell left, not the row it used to occupy.
        const QModelIndex first = picker.targetsModel()->index(0, 0);
        QCOMPARE(picker.targetsModel()->data(first, TargetModel::ShortcutRole).toString(), QStringLiteral("1"));
        QCOMPARE(picker.targetsModel()->data(first, TargetModel::LabelRole).toString(), QStringLiteral("Chromium"));
    }

    void everyTermMustMatchButNotAllInTheSameField()
    {
        RuleStore store;
        FakeLauncher launcher;
        PickerController picker(&store, nullptr, &launcher);
        populateMany(picker);

        // Label and profile name, one term each.
        picker.setFilter(QStringLiteral("fire work"));
        QCOMPARE(picker.targetsModel()->rowCount(), 1);

        // The desktop id is searchable because that is what --list prints and
        // what rules are written against.
        picker.setFilter(QStringLiteral("chromium.desktop"));
        QCOMPARE(picker.targetsModel()->rowCount(), 1);

        // Every term has to land somewhere.
        picker.setFilter(QStringLiteral("firefox chromium"));
        QCOMPARE(picker.targetsModel()->rowCount(), 0);
    }

    void narrowingKeepsTheHighlightOnTheSameTarget()
    {
        RuleStore store;
        FakeLauncher launcher;
        PickerController picker(&store, nullptr, &launcher);
        populateMany(picker);

        picker.setCurrentIndex(2); // Chromium
        picker.setFilter(QStringLiteral("chrom"));
        // Still on Chromium, which is now row 0 -- the selection follows the
        // target rather than the row number.
        QCOMPARE(picker.currentIndex(), 0);
        QCOMPARE(picker.targetsModel()->data(picker.targetsModel()->index(0, 0), TargetModel::LabelRole).toString(),
                 QStringLiteral("Chromium"));

        // A filter that hides the selection takes the highlight to the top
        // rather than leaving it pointing at a row that is not shown.
        picker.setFilter(QStringLiteral("firefox"));
        QCOMPARE(picker.currentIndex(), 0);
    }

    void afilterThatMatchesNothingSelectsNothingAndChoosesNothing()
    {
        RuleStore store;
        FakeLauncher launcher;
        PickerController picker(&store, nullptr, &launcher);
        populateMany(picker);
        picker.showPicker(Link::plain(url), QString());

        picker.setFilter(QStringLiteral("nothingmatchesthis"));
        QCOMPARE(picker.targetsModel()->rowCount(), 0);
        QCOMPARE(picker.currentIndex(), -1);

        // Enter on an empty list must not launch whatever used to be row 0.
        picker.choose(picker.currentIndex(), false, int(MemoryScope::None));
        QCOMPARE(launcher.callbacks.size(), 0);

        // The browsers are still there; the filter is what is hiding them, and
        // the picker has to be able to say so.
        QCOMPARE(picker.unfilteredCount(), 3);
    }

    void eachLinkStartsUnfiltered()
    {
        RuleStore store;
        FakeLauncher launcher;
        PickerController picker(&store, nullptr, &launcher);
        populateMany(picker);

        picker.showPicker(Link::plain(url), QString());
        picker.setFilter(QStringLiteral("chrom"));
        QCOMPARE(picker.targetsModel()->rowCount(), 1);

        // A filter left over from the previous link would hide browsers with
        // no explanation on screen for why.
        picker.showPicker(Link::plain(QUrl(QStringLiteral("https://other.example/"))), QString());
        QCOMPARE(picker.filter(), QString());
        QCOMPARE(picker.targetsModel()->rowCount(), 3);
    }

    void enablingAnOtherHandlerRefiltersWithoutRediscovering()
    {
        RuleStore store;
        FakeLauncher launcher;
        PickerController picker(&store, nullptr, &launcher);

        Target browser;
        browser.id = QStringLiteral("browser.desktop");
        browser.storageId = browser.id;
        browser.label = QStringLiteral("Browser");
        browser.kind = TargetKind::Browser;

        Target chat;
        chat.id = QStringLiteral("chat.desktop");
        chat.storageId = chat.id;
        chat.label = QStringLiteral("Chat");
        chat.kind = TargetKind::OtherHandler;

        // Stands in for what discovery found: both exist on the system.
        picker.setTargets({browser, chat});

        // A handler that is not a browser stays hidden until it is named, and
        // this runs off what discovery already returned -- there is no KSycoca
        // database in a test, so a rediscovery here would empty the list and
        // the assertions below would pass for the wrong reason.
        picker.refreshEnabledHandlers();
        QCOMPARE(picker.targetsModel()->rowCount(), 1);
        // Hidden means hidden all the way: a rule naming it does not resolve
        // either, so enabling it is what makes it routable at all.
        QVERIFY(picker.targetById(chat.id).id.isEmpty());

        QVERIFY(store.setOtherHandlerEnabled(chat.id, true));
        picker.refreshEnabledHandlers();
        QCOMPARE(picker.targetsModel()->rowCount(), 2);

        QVERIFY(store.setOtherHandlerEnabled(chat.id, false));
        picker.refreshEnabledHandlers();
        QCOMPARE(picker.targetsModel()->rowCount(), 1);
    }

    void repeatedSelectionCompletesOnceAndRemembersAfterSuccess()
    {
        RuleStore store;
        FakeLauncher launcher;
        PickerController picker(&store, nullptr, &launcher);
        populate(picker);
        QSignalSpy finished(&picker, &PickerController::finished);
        picker.showPicker(Link::plain(url), {});
        picker.choose(0, false, int(MemoryScope::Host));
        picker.choose(0, false, int(MemoryScope::Host));
        QCOMPARE(launcher.callbacks.size(), 1);
        QCOMPARE(store.memoryIndexFor(url), -1);
        QCOMPARE(finished.count(), 0);
        launcher.callbacks[0]({});
        launcher.callbacks[0]({});
        QCOMPARE(finished.count(), 1);
        QVERIFY(store.memoryIndexFor(url) >= 0);
    }

    void failureKeepsUrlForRetry()
    {
        RuleStore store;
        FakeLauncher launcher;
        PickerController picker(&store, nullptr, &launcher);
        populate(picker);
        QSignalSpy finished(&picker, &PickerController::finished);
        picker.showPicker(Link::plain(url), {});
        picker.choose(0, false, int(MemoryScope::Host));
        launcher.callbacks[0]({LaunchResult::Failed, QStringLiteral("failed")});
        QCOMPARE(picker.mode(), PickerController::Mode::Picker);
        QCOMPARE(picker.url(), url.toString());
        QVERIFY(!picker.errorMessage().isEmpty());
        QCOMPARE(store.memoryIndexFor(url), -1);
        QCOMPARE(finished.count(), 0);
        picker.choose(0, false, int(MemoryScope::None));
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
        picker.choose(0, false, int(MemoryScope::None));
        picker.cancel();
        QVERIFY(launcher.operations[0]->cancelled);
        QCOMPARE(finished.count(), 1);
        picker.showPicker(Link::plain(QUrl(QStringLiteral("https://next.example/"))), {});
        launcher.callbacks[0]({LaunchResult::Cancelled, {}});
        QCOMPARE(finished.count(), 1);
        QCOMPARE(picker.mode(), PickerController::Mode::Picker);
        picker.choose(0, false, int(MemoryScope::None));
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
        QVERIFY(store.remember(MemoryScope::Host, url, QStringLiteral("test.desktop"), false));
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
        picker.choose(0, false, int(MemoryScope::None));
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

        picker.choose(0, false, int(MemoryScope::Host));
        QCOMPARE(launcher.urls.size(), 1);
        QCOMPARE(launcher.urls.constFirst(), scanner);
        launcher.callbacks[0]({});
        QVERIFY(store.memoryIndexFor(QUrl(QStringLiteral("https://github.com/kde/plasma"))) >= 0);
        QCOMPARE(store.memoryIndexFor(QUrl(QStringLiteral("https://eu01.safelinks.protection.outlook.com/x"))), -1);
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
        picker.choose(0, false, int(MemoryScope::None));
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
