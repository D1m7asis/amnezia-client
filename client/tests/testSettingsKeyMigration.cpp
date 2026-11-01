#include "settingsKeyMigration.h"
#include <QtTest>

class TestSettingsKeyMigration : public QObject {
    Q_OBJECT
private slots:
    void migration_data()
    {
        QTest::addColumn<QString>("failure");
        for (const char *name : { "none", "vault", "legacy", "corrupt", "backup", "write", "scoped-corrupt" })
            QTest::newRow(name) << QString(name);
    }
    void migration()
    {
        QFETCH(QString, failure);
        using namespace settingsKeys;
        const QByteArray key(32, 'k'), iv(32, 'i'), original = key + iv;
        QByteArray scoped;
        int writes = 0, backups = 0, generations = 0, legacyReads = 0;
        if (failure == "scoped-corrupt") scoped = QByteArray(64, 'x');
        Store store {
            [&]() -> Read { return failure == "vault" ? Read { Status::Error, {} }
                : Read { scoped.isEmpty() ? Status::Missing : Status::Found, scoped }; },
            [&]() -> Read { ++legacyReads; return { failure == "legacy" ? Status::Error : Status::Found, key }; },
            [&]() -> Read { ++legacyReads; return { Status::Found, iv }; },
            [&](const QByteArray &bytes) { return failure != "corrupt" && bytes == original; },
            [&](const QByteArray &bytes) { ++backups; return bytes == original && failure != "backup"; },
            [&](const QByteArray &bytes) { ++writes; if (failure == "write") return false; scoped = bytes; return true; },
            [&]() { ++generations; return QByteArray(64, 'n'); }
        };
        const auto result = load(store, true);
        QCOMPARE(generations, 0);
        if (failure == "none") {
            QCOMPARE(result, original);
            QCOMPARE(writes, 1);
            QCOMPARE(backups, 1);
            const int reads = legacyReads;
            QCOMPARE(load(store, true), original);
            QCOMPARE(writes, 1);
            QCOMPARE(legacyReads, reads);
        } else {
            QVERIFY(result.isEmpty());
            if (failure != "write") QCOMPARE(writes, 0);
        }
        QCOMPARE(key, QByteArray(32, 'k'));
        QCOMPARE(iv, QByteArray(32, 'i'));
    }
    void freshStoreDoesNotUseLegacyKeys()
    {
        using namespace settingsKeys;
        QByteArray scoped;
        Store store {
            [&]() -> Read { return { scoped.isEmpty() ? Status::Missing : Status::Found, scoped }; },
            []() -> Read { QTest::qFail("Fresh store read legacy key", __FILE__, __LINE__); return { Status::Error, {} }; },
            []() -> Read { QTest::qFail("Fresh store read legacy IV", __FILE__, __LINE__); return { Status::Error, {} }; },
            [](const QByteArray &) { return true; }, [](const QByteArray &) { return false; },
            [&](const QByteArray &bytes) { scoped = bytes; return true; },
            []() { return QByteArray(64, 'n'); }
        };
        QCOMPARE(load(store, false), QByteArray(64, 'n'));
    }
};
QTEST_GUILESS_MAIN(TestSettingsKeyMigration)
#include "testSettingsKeyMigration.moc"
