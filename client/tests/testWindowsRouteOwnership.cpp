#include "platforms/windows/daemon/windowsrouteownership.h"
#include <QtTest>

class TestWindowsRouteOwnership : public QObject {
    Q_OBJECT
private slots:
    void existingRouteIsNeverDeleted()
    {
        MIB_IPFORWARD_ROW2 row {};
        row.InterfaceLuid.Value = 42;
        row.DestinationPrefix.Prefix.si_family = AF_INET;
        row.DestinationPrefix.PrefixLength = 32;
        row.NextHop.si_family = AF_INET;
        bool owned = true;
        int deletes = 0;
        auto remove = [&](const MIB_IPFORWARD_ROW2 *) -> DWORD { ++deletes; return NO_ERROR; };
        QCOMPARE(acquireWindowsRoute(&row, owned,
            [](const MIB_IPFORWARD_ROW2 *) -> DWORD { return ERROR_OBJECT_ALREADY_EXISTS; },
            [](MIB_IPFORWARD_ROW2 *existing) -> DWORD { existing->Metric = 19; return NO_ERROR; }), DWORD(NO_ERROR));
        QVERIFY(!owned);
        QCOMPARE(releaseWindowsRoute(&row, owned, remove), DWORD(NO_ERROR));
        QCOMPARE(deletes, 0);
        QCOMPARE(acquireWindowsRoute(&row, owned,
            [](const MIB_IPFORWARD_ROW2 *) -> DWORD { return NO_ERROR; }), DWORD(NO_ERROR));
        QVERIFY(owned);
        QCOMPARE(releaseWindowsRoute(&row, owned, remove), DWORD(NO_ERROR));
        QCOMPARE(deletes, 1);
    }
    void mismatchedRouteIsNotAccepted()
    {
        MIB_IPFORWARD_ROW2 row {};
        row.InterfaceLuid.Value = 42;
        bool owned = true;
        QCOMPARE(acquireWindowsRoute(&row, owned,
            [](const MIB_IPFORWARD_ROW2 *) -> DWORD { return ERROR_OBJECT_ALREADY_EXISTS; },
            [](MIB_IPFORWARD_ROW2 *existing) -> DWORD { existing->InterfaceLuid.Value = 99; return NO_ERROR; }), DWORD(ERROR_INVALID_DATA));
        QVERIFY(!owned);
    }
};
QTEST_GUILESS_MAIN(TestWindowsRouteOwnership)
#include "testWindowsRouteOwnership.moc"
