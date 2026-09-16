#include "core/DefaultBrowserManager.h"
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QTemporaryDir>
#include <QFile>
#include <QTest>

using namespace Lob;

class DefaultBrowserManagerTest : public QObject
{
    Q_OBJECT
    QTemporaryDir configDir;
    const QString http = QStringLiteral("x-scheme-handler/http");
    const QString https = QStringLiteral("x-scheme-handler/https");
    const QString own = QStringLiteral(LOB_APP_ID ".desktop");
    QStringList effective;
    QStringList missing;

    AssociationBackend backend()
    {
        return {[this] { return effective; }, [this](const QString &id) { return !missing.contains(id); }};
    }
    QString path() const { return configDir.filePath(QStringLiteral("mimeapps.list")); }
    void setDefault(const QString &type, const QString &id)
    {
        KConfig config(path(), KConfig::SimpleConfig);
        KConfigGroup group(&config, QStringLiteral("Default Applications"));
        group.writeXdgListEntry(type, {id});
        QVERIFY(config.sync());
    }
    QStringList defaults(const QString &type)
    {
        KConfig config(path(), KConfig::SimpleConfig);
        return KConfigGroup(&config, QStringLiteral("Default Applications")).readXdgListEntry(type);
    }
private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(configDir.isValid());
        qputenv("XDG_CONFIG_HOME", configDir.path().toUtf8());
    }
    void init()
    {
        QFile::remove(path());
        QFile::remove(configDir.filePath(QStringLiteral("lobstate")));
        effective = {QStringLiteral("a.desktop"), QStringLiteral("b.desktop")};
        missing.clear();
    }

    void repeatedClaimRestoresEachScheme()
    {
        setDefault(http, effective[0]);
        setDefault(https, effective[1]);
        QString error;
        QVERIFY2(DefaultBrowserManager::claim(&error, backend()), qPrintable(error));
        QCOMPARE(defaults(http), QStringList{own});
        effective = {own, own};
        QVERIFY(DefaultBrowserManager::claim(&error, backend()));
        QCOMPARE(DefaultBrowserManager::previousHandler(QStringLiteral("http")), QStringLiteral("a.desktop"));
        QCOMPARE(DefaultBrowserManager::previousHandler(QStringLiteral("https")), QStringLiteral("b.desktop"));
        QVERIFY2(DefaultBrowserManager::restore(&error, backend()), qPrintable(error));
        QCOMPARE(defaults(http), QStringList{QStringLiteral("a.desktop")});
        QCOMPARE(defaults(https), QStringList{QStringLiteral("b.desktop")});
    }

    void unsetLocalDefaultIsRemovedOnRestore()
    {
        effective = {QString(), QString()};
        QVERIFY(DefaultBrowserManager::claim(nullptr, backend()));
        QVERIFY(DefaultBrowserManager::restore(nullptr, backend()));
        KConfig config(path(), KConfig::SimpleConfig);
        KConfigGroup group(&config, QStringLiteral("Default Applications"));
        QVERIFY(!group.hasKey(http));
        QVERIFY(!group.hasKey(https));
    }

    void partialFailureRetainsOnlyUnresolvedBackup()
    {
        setDefault(http, effective[0]);
        setDefault(https, effective[1]);
        QVERIFY(DefaultBrowserManager::claim(nullptr, backend()));
        missing = {QStringLiteral("b.desktop")};
        QString error;
        QVERIFY(!DefaultBrowserManager::restore(&error, backend()));
        QVERIFY(!error.isEmpty());
        QCOMPARE(defaults(http), QStringList{QStringLiteral("a.desktop")});
        QCOMPARE(defaults(https), QStringList{own});
        QCOMPARE(DefaultBrowserManager::previousHandler(QStringLiteral("https")), QStringLiteral("b.desktop"));
        missing.clear();
        QVERIFY(DefaultBrowserManager::restore(&error, backend()));
        QCOMPARE(defaults(https), QStringList{QStringLiteral("b.desktop")});
    }

    void laterUserChoiceIsPreserved()
    {
        setDefault(http, effective[0]);
        setDefault(https, effective[1]);
        QVERIFY(DefaultBrowserManager::claim(nullptr, backend()));
        setDefault(https, QStringLiteral("user-choice.desktop"));
        QVERIFY(DefaultBrowserManager::restore(nullptr, backend()));
        QCOMPARE(defaults(https), QStringList{QStringLiteral("user-choice.desktop")});
    }

    void reclaimRecordsFreshLocalChoiceEvenWithStaleCache()
    {
        setDefault(http, effective[0]);
        setDefault(https, effective[1]);
        QVERIFY(DefaultBrowserManager::claim(nullptr, backend()));
        effective = {own, own};
        setDefault(https, QStringLiteral("new-choice.desktop"));
        QVERIFY(DefaultBrowserManager::claim(nullptr, backend()));
        QVERIFY(DefaultBrowserManager::restore(nullptr, backend()));
        QCOMPARE(defaults(http), QStringList{QStringLiteral("a.desktop")});
        QCOMPARE(defaults(https), QStringList{QStringLiteral("new-choice.desktop")});
    }

    void oldReleaseBackupStillRestores()
    {
        setDefault(http, own);
        setDefault(https, own);
        {
            KConfig state(configDir.filePath(QStringLiteral("lobstate")), KConfig::SimpleConfig);
            KConfigGroup(&state, QStringLiteral("PreviousHandlers")).writeEntry("handlers", effective);
            QVERIFY(state.sync());
        }
        QVERIFY(DefaultBrowserManager::restore(nullptr, backend()));
        QCOMPARE(defaults(http), QStringList{effective[0]});
        QCOMPARE(defaults(https), QStringList{effective[1]});
    }
};
QTEST_GUILESS_MAIN(DefaultBrowserManagerTest)
#include "DefaultBrowserManagerTest.moc"
