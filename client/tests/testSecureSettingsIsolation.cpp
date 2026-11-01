#include "secureQSettings.h"
#include <windows.h>
#include <wincred.h>
#include <QUuid>
#include <QScopeGuard>
#include <QtTest>

class TestSecureSettingsIsolation : public QObject {
    Q_OBJECT
private slots:
    void restartAndMissingCredentialPreserveProfiles()
    {
        const QString organization = "D1m7asis.SettingsTest." + QUuid::createUuid().toString(QUuid::Id128);
        const QString application = "Isolated";
        const QString target = "D1m7asisVPN-Keychain-Tests/" + organization + "/" + application + "/settingsMaterial-v1";
        QSettings raw(organization, application);
        const auto cleanup = qScopeGuard([&]() {
            CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0);
            raw.clear();
            raw.sync();
        });
        const QByteArray profiles("[{\"description\":\"test-one\"},{\"description\":\"test-two\"}]");
        {
            SecureQSettings settings(organization, application);
            settings.setValue("Servers/serversList", profiles);
            QCOMPARE(settings.value("Servers/serversList").toByteArray(), profiles);
        }
        raw.sync();
        const auto encrypted = raw.value("Servers/serversList");
        QVERIFY(encrypted.toByteArray().startsWith("EncData"));
        {
            SecureQSettings restarted(organization, application);
            QVERIFY(restarted.profileStoreReadable());
            QCOMPARE(restarted.value("Servers/serversList").toByteArray(), profiles);
        }
        raw.setValue("Servers/serversList", QByteArray("EncData-corrupt"));
        raw.sync();
        {
            SecureQSettings corrupt(organization, application);
            QVERIFY(!corrupt.profileStoreReadable());
            corrupt.setValue("Servers/serversList", QByteArray("[]"));
        }
        raw.sync();
        QCOMPARE(raw.value("Servers/serversList").toByteArray(), QByteArray("EncData-corrupt"));
        raw.setValue("Servers/serversList", encrypted);
        raw.sync();
        const bool removed = CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0);
        {
            SecureQSettings missing(organization, application);
            QVERIFY(!missing.profileStoreReadable());
            missing.setValue("Servers/serversList", QByteArray("[]"));
        }
        raw.sync();
        const bool preserved = raw.value("Servers/serversList") == encrypted;
        PCREDENTIALW credential = nullptr;
        const bool regenerated = CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0, &credential);
        if (credential) CredFree(credential);
        // Always clean only this test's UUID-scoped data.
        CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0);
        raw.clear();
        raw.sync();
        QVERIFY(removed);
        QVERIFY(preserved);
        QVERIFY(!regenerated);
    }
};
QTEST_GUILESS_MAIN(TestSecureSettingsIsolation)
#include "testSecureSettingsIsolation.moc"
