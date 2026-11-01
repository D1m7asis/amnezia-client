#include <QtTest>
#include <windows.h>
#include "core/utils/windowsVpnConflict.h"

class WindowsVpnConflictTest : public QObject {
    Q_OBJECT
private slots:
    void detectsOnlyActiveOfficialTunnel()
    {
        using namespace windowsVpnConflict;
        const auto up = QNetworkInterface::IsUp | QNetworkInterface::IsRunning;
        const QList<QHostAddress> vpn { QHostAddress("10.8.1.6") };
        QVERIFY(isActiveOfficialTunnel("AmneziaVPN", up, vpn));
        QVERIFY(isActiveOfficialTunnel("amneziavpn", up, vpn));
        QVERIFY(!isActiveOfficialTunnel("AmneziaVPN", {}, vpn));
        QVERIFY(!isActiveOfficialTunnel("AmneziaVPN", QNetworkInterface::IsUp, vpn));
        QVERIFY(!isActiveOfficialTunnel("AmneziaVPN", up, {}));
        QVERIFY(!isActiveOfficialTunnel("AmneziaVPN", up, {QHostAddress::LocalHost}));
        QVERIFY(!isActiveOfficialTunnel("AmneziaVPN", up, {QHostAddress("169.254.1.2")}));
        for (const auto &name : {"D1m7asisVPN", "d1misxray", "Ethernet", "Radmin VPN"}) {
            QVERIFY(!isActiveOfficialTunnel(name, up, vpn));
        }
        QVERIFY(isActiveOfficialTunnel("AmneziaVPN", up, vpn));
        QVERIFY(!isActiveOfficialTunnel("AmneziaVPN", {}, vpn));
    }
};
QTEST_GUILESS_MAIN(WindowsVpnConflictTest)
#include "testWindowsVpnConflict.moc"
