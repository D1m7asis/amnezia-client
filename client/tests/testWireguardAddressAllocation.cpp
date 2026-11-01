#include "core/configurators/wireguardConfigurator.h"
#include <QtTest>

class TestWireguardAddressAllocation : public QObject
{
    Q_OBJECT
private slots:
    void validatesCachedManagedConfig()
    {
        amnezia::AwgProtocolConfig config;
        config.serverConfig.subnetAddress = "10.8.1.0";
        config.serverConfig.subnetCidr = "24";
        config.clientConfig = amnezia::AwgClientConfig {};
        config.clientConfig->clientIp = "10.8.2.1/32";
        QVERIFY(!WireguardConfigurator::hasCompatibleClientAddress(config));
        config.clientConfig->clientIp = "10.8.1.6/32";
        QVERIFY(WireguardConfigurator::hasCompatibleClientAddress(config));
        config.clientConfig->clientIp = "10.8.1.255/32";
        QVERIFY(!WireguardConfigurator::hasCompatibleClientAddress(config));
        config.serverConfig.isThirdPartyConfig = true;
        QVERIFY(WireguardConfigurator::hasCompatibleClientAddress(config));
        config.serverConfig.isThirdPartyConfig = false;
        config.serverConfig.subnetAddress.clear();
        QVERIFY(WireguardConfigurator::hasCompatibleClientAddress(config));
    }

    void reusesFreeAddressInsideSubnet()
    {
        const QList<QHostAddress> used { QHostAddress("10.8.1.1"), QHostAddress("10.8.1.254"), QHostAddress("10.8.2.1") };
        QCOMPARE(WireguardConfigurator::nextClientAddress(QHostAddress("10.8.1.0"), 24, used), QHostAddress("10.8.1.2"));
    }
    void reservesActualServerAddress()
    {
        QCOMPARE(WireguardConfigurator::nextClientAddress(QHostAddress("10.8.1.1"), 24, {}), QHostAddress("10.8.1.2"));
    }
    void fullSubnetCannotOverflow()
    {
        const QList<QHostAddress> used { QHostAddress("10.8.1.2") };
        QVERIFY(WireguardConfigurator::nextClientAddress(QHostAddress("10.8.1.1"), 30, used).isNull());
        QVERIFY(WireguardConfigurator::nextClientAddress(QHostAddress("10.8.1.0"), 31, {}).isNull());
        QVERIFY(WireguardConfigurator::nextClientAddress(QHostAddress("::1"), 24, {}).isNull());
    }
};
QTEST_GUILESS_MAIN(TestWireguardAddressAllocation)
#include "testWireguardAddressAllocation.moc"
