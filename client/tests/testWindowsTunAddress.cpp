#include "../../service/server/windowsTunAddress.h"
#include <QtTest>

class TestWindowsTunAddress : public QObject {
    Q_OBJECT
private slots:
    void assignment_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<quint32>("expected");
        QTest::newRow("already-preferred") << QStringLiteral("existing") << quint32(NO_ERROR);
        QTest::newRow("new-address") << QStringLiteral("fresh") << quint32(NO_ERROR);
        QTest::newRow("lost-notification") << QStringLiteral("later") << quint32(NO_ERROR);
        QTest::newRow("last-state-check") << QStringLiteral("last") << quint32(NO_ERROR);
        QTest::newRow("real-timeout") << QStringLiteral("timeout") << quint32(ERROR_TIMEOUT);
        QTest::newRow("wrong-interface") << QStringLiteral("interface") << quint32(ERROR_TIMEOUT);
        QTest::newRow("wrong-address") << QStringLiteral("address") << quint32(ERROR_TIMEOUT);
        QTest::newRow("dad-not-ready") << QStringLiteral("dad") << quint32(ERROR_TIMEOUT);
        QTest::newRow("missing-interface") << QStringLiteral("missing") << quint32(ERROR_NOT_FOUND);
        QTest::newRow("create-failed") << QStringLiteral("denied") << quint32(ERROR_ACCESS_DENIED);
    }
    void assignment()
    {
        QFETCH(QString, mode);
        QFETCH(quint32, expected);
        int queries = 0, creates = 0, sleeps = 0;
        WindowsTunAddressApi api;
        api.resolve = [&](const wchar_t *, NET_LUID *luid) -> DWORD {
            luid->Value = 42;
            return mode == "missing" ? ERROR_NOT_FOUND : NO_ERROR;
        };
        api.query = [&](MIB_UNICASTIPADDRESS_ROW *row) -> DWORD {
            ++queries;
            if (mode == "timeout" || mode == "denied"
                || (mode == "later" && queries < 3) || (mode == "fresh" && queries == 1) || (mode == "last" && queries < 502))
                return ERROR_NOT_FOUND;
            row->DadState = mode == "dad" ? IpDadStateTentative : IpDadStatePreferred;
            if (mode == "interface") row->InterfaceLuid.Value = 99;
            if (mode == "address") row->Address.Ipv4.sin_addr.s_addr = 0;
            return NO_ERROR;
        };
        api.create = [&](const MIB_UNICASTIPADDRESS_ROW *) -> DWORD {
            ++creates;
            return mode == "denied" ? ERROR_ACCESS_DENIED : mode == "fresh" ? NO_ERROR : ERROR_OBJECT_ALREADY_EXISTS;
        };
        api.sleep = [&](DWORD) { ++sleeps; };
        bool created = true;
        QCOMPARE(assignWindowsTunAddress("owned-test", "10.33.0.2", api, &created), DWORD(expected));
        QCOMPARE(created, mode == "fresh");
        if (mode == "existing") { QCOMPARE(creates, 0); QCOMPARE(sleeps, 0); }
        if (mode == "missing") QCOMPARE(creates, 0);
        // Each retry starts with fresh state; a failed attempt cannot poison it.
        mode = "existing";
        QCOMPARE(assignWindowsTunAddress("owned-test", "10.33.0.2", api), DWORD(NO_ERROR));
    }
};
QTEST_GUILESS_MAIN(TestWindowsTunAddress)
#include "testWindowsTunAddress.moc"
