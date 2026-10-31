#include "platforms/windows/daemon/windowsroutemonitor.h"
#include <QScopeGuard>
#include <QtTest>

class TestWindowsEndpointRoute : public QObject
{
    Q_OBJECT
private slots:
    void endpointRouteIsCreatedAndRemoved()
    {
        if (qEnvironmentVariable("D1MISVPN_TEST_ROUTE_ADMIN") != "1")
            QSKIP("Opt-in elevated route test uses only TEST-NET-1");
        bool ok = false;
        const auto index = qEnvironmentVariable("D1MISVPN_TEST_EXCLUDE_INTERFACE").toUInt(&ok);
        QVERIFY(ok);
        NET_LUID excluded {};
        QCOMPARE(ConvertInterfaceIndexToLuid(index, &excluded), DWORD(NO_ERROR));
        const IPAddress target(QStringLiteral("192.0.2.47/32"));
        auto find = [&]() {
            QList<MIB_IPFORWARD_ROW2> matches;
            PMIB_IPFORWARD_TABLE2 table = nullptr;
            if (GetIpForwardTable2(AF_INET, &table) != NO_ERROR)
                return matches;
            for (ULONG i = 0; i < table->NumEntries; ++i) {
                const auto &row = table->Table[i];
                if (row.DestinationPrefix.PrefixLength == 32
                    && ntohl(row.DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr) == 0xc000022f)
                    matches.append(row);
            }
            FreeMibTable(table);
            return matches;
        };
        QVERIFY2(find().isEmpty(), "TEST-NET route already exists; preserve it");
        MIB_IPFORWARD_ROW2 foreign {};
        {
            WindowsRouteMonitor monitor(excluded.Value, nullptr);
            QVERIFY(monitor.addExclusionRoute(IPAddress(QString())));
            QVERIFY(monitor.addExclusionRoute(target));
            QVERIFY(monitor.addExclusionRoute(target));
            const auto routes = find();
            QCOMPARE(routes.size(), 1);
            QVERIFY(routes.first().InterfaceLuid.Value != excluded.Value);
            QVERIFY(routes.first().NextHop.Ipv4.sin_addr.s_addr != 0);
            foreign = routes.first();
        }
        QVERIFY2(find().isEmpty(), "Owned route must be removed on shutdown");
        foreign.Metric = 19;
        QCOMPARE(CreateIpForwardEntry2(&foreign), DWORD(NO_ERROR));
        const auto cleanup = qScopeGuard([&] { DeleteIpForwardEntry2(&foreign); });
        {
            WindowsRouteMonitor monitor(excluded.Value, nullptr);
            QVERIFY(monitor.addExclusionRoute(target));
        }
        const auto preserved = find();
        QCOMPARE(preserved.size(), 1);
        QCOMPARE(preserved.first().Metric, ULONG(19));
        QCOMPARE(preserved.first().InterfaceLuid.Value, foreign.InterfaceLuid.Value);
    }
};
QTEST_GUILESS_MAIN(TestWindowsEndpointRoute)
#include "testWindowsEndpointRoute.moc"
