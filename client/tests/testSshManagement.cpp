#include <QJsonArray>
#include <QTest>

#include "core/models/selfhosted/selfHostedAdminServerConfig.h"
#include "core/models/selfhosted/selfHostedUserServerConfig.h"
#include "core/models/selfhosted/sshManagementConfig.h"
#include "core/utils/constants/configKeys.h"

using namespace amnezia;

namespace
{
    SshManagementConfig managementConfig()
    {
        SshManagementConfig config;
        config.transport = QStringLiteral("xray");
        config.targetHost = QStringLiteral("172.29.172.1");
        config.targetPort = 2222;
        config.hostKeySha256 = QStringLiteral("SHA256:")
                + QString::fromLatin1(QByteArray(32, 'x').toBase64(QByteArray::OmitTrailingEquals));
        const QJsonObject server {
            { QStringLiteral("address"), QStringLiteral("203.0.113.10") },
            { QStringLiteral("port"), 443 },
            { QStringLiteral("users"),
              QJsonArray {
                      QJsonObject { { QStringLiteral("id"), QStringLiteral("00000000-0000-4000-8000-000000000001") } } } }
        };
        const QJsonObject stream {
            { QStringLiteral("network"), QStringLiteral("xhttp") },
            { QStringLiteral("security"), QStringLiteral("reality") },
            { QStringLiteral("realitySettings"),
              QJsonObject { { QStringLiteral("publicKey"),
                              QString::fromLatin1(QByteArray(32, 'y').toBase64(QByteArray::Base64UrlEncoding
                                                                               | QByteArray::OmitTrailingEquals)) },
                            { QStringLiteral("serverName"), QStringLiteral("example.com") } } },
            { QStringLiteral("xhttpSettings"), QJsonObject { { QStringLiteral("mode"), QStringLiteral("stream-one") } } }
        };
        const QJsonObject outbound { { QStringLiteral("protocol"), QStringLiteral("vless") },
                                     { QStringLiteral("settings"),
                                       QJsonObject { { QStringLiteral("vnext"), QJsonArray { server } } } },
                                     { QStringLiteral("streamSettings"), stream } };
        config.xrayConfig = { { QStringLiteral("outbounds"), QJsonArray { outbound } } };
        return config;
    }

    SelfHostedAdminServerConfig adminConfig()
    {
        SelfHostedAdminServerConfig config;
        config.hostName = QStringLiteral("203.0.113.10");
        config.userName = QStringLiteral("root");
        config.password = QStringLiteral("synthetic-test-secret");
        config.port = 22;
        config.defaultContainer = ContainerEnumNS::DockerContainer::Xray;
        return config;
    }
}

class TestSshManagement : public QObject
{
    Q_OBJECT
private slots:
    void legacyProfilesRemainDirect()
    {
        const auto original = adminConfig();
        QVERIFY(!original.toJson().contains(configKey::sshManagement));
        const auto loaded = SelfHostedAdminServerConfig::fromJson(original.toJson());
        QCOMPARE(loaded.credentials().sshManagement.transport, QStringLiteral("direct"));
        QVERIFY(loaded.credentials().isValid());
    }

    void managementTargetDoesNotReplacePublicEndpoint()
    {
        auto original = adminConfig();
        original.sshManagement = managementConfig();
        const auto loaded = SelfHostedAdminServerConfig::fromJson(original.toJson());
        QVERIFY(loaded.credentials().isValid());
        QCOMPARE(loaded.credentials().hostName, original.hostName);
        QCOMPARE(loaded.credentials().port, 22);
        QCOMPARE(loaded.toJson().value(configKey::hostName).toString(), original.hostName);
        QCOMPARE(loaded.credentials().sshManagement.targetHost, QStringLiteral("172.29.172.1"));
        QCOMPARE(loaded.credentials().sshManagement.targetPort, 2222);
        QCOMPARE(loaded.sshManagement.toJson(), original.sshManagement.toJson());
    }

    void guestSerializationDoesNotCarrySshManagement()
    {
        auto original = adminConfig();
        original.sshManagement = managementConfig();
        const auto guest = SelfHostedUserServerConfig::fromJson(original.toJson()).toJson();
        QVERIFY(!guest.contains(configKey::sshManagement));
        QVERIFY(!guest.contains(configKey::password));
        QCOMPARE(guest.value(configKey::hostName).toString(), original.hostName);
    }

    void missingHostPinIsRejected()
    {
        auto config = managementConfig();
        QVERIFY(config.isValid());
        config.hostKeySha256.clear();
        QVERIFY(!config.isValid());
        config.hostKeySha256 = QStringLiteral("SHA256:invalid");
        QVERIFY(!config.isValid());
    }

    void unknownModeAndMalformedRootCannotFallBackToDirect()
    {
        auto original = adminConfig();
        auto json = original.toJson();
        json[configKey::sshManagement] = QJsonArray {};
        QVERIFY(!SelfHostedAdminServerConfig::fromJson(json).credentials().isValid());
        json[configKey::sshManagement] = QJsonObject { { QStringLiteral("transport"), QStringLiteral("unrecognized") } };
        QVERIFY(!SelfHostedAdminServerConfig::fromJson(json).credentials().isValid());
    }

    void ambiguousOrNonRealityProfileIsRejected()
    {
        auto config = managementConfig();
        const auto outbound = config.realityOutbound();
        config.xrayConfig[QStringLiteral("outbounds")] = QJsonArray { outbound, outbound };
        QVERIFY(!config.isValid());
        config.xrayConfig[QStringLiteral("outbounds")] =
                QJsonArray { QJsonObject { { QStringLiteral("protocol"), QStringLiteral("freedom") } } };
        QVERIFY(!config.isValid());
    }

    void fractionalAndOutOfRangePortsAreRejected()
    {
        const auto valid = managementConfig().toJson();
        for (const QJsonValue value :
             { QJsonValue(1.5), QJsonValue(-1), QJsonValue(65536), QJsonValue(QStringLiteral("2222")) }) {
            auto json = valid;
            json[QStringLiteral("targetPort")] = value;
            QVERIFY(!SshManagementConfig::fromJson(json).isValid());
        }
    }

    void forwardingDropsRoutingDetoursAndBindsOnlyLoopback()
    {
        auto config = managementConfig();
        auto outbound = config.realityOutbound();
        outbound[QStringLiteral("proxySettings")] =
                QJsonObject { { QStringLiteral("tag"), QStringLiteral("other-proxy") } };
        outbound[QStringLiteral("sendThrough")] = QStringLiteral("192.0.2.1");
        auto stream = outbound[QStringLiteral("streamSettings")].toObject();
        stream[QStringLiteral("sockopt")] = QJsonObject { { QStringLiteral("dialerProxy"), QStringLiteral("direct") } };
        outbound[QStringLiteral("streamSettings")] = stream;
        config.xrayConfig[QStringLiteral("outbounds")] =
                QJsonArray { outbound, QJsonObject { { QStringLiteral("protocol"), QStringLiteral("freedom") } } };
        config.xrayConfig[QStringLiteral("routing")] =
                QJsonObject { { QStringLiteral("domainStrategy"), QStringLiteral("AsIs") } };
        const auto forwarded = config.forwardingConfig(QStringLiteral("203.0.113.10"), 22, 22022);
        QVERIFY(!forwarded.isEmpty());
        QVERIFY(!forwarded.contains(QStringLiteral("routing")));
        QCOMPARE(forwarded[QStringLiteral("outbounds")].toArray().size(), 1);
        const auto clean = forwarded[QStringLiteral("outbounds")].toArray().first().toObject();
        QVERIFY(!clean.contains(QStringLiteral("proxySettings")));
        QVERIFY(!clean.contains(QStringLiteral("sendThrough")));
        QVERIFY(!clean[QStringLiteral("streamSettings")].toObject().contains(QStringLiteral("sockopt")));
        const auto inbound = forwarded[QStringLiteral("inbounds")].toArray().first().toObject();
        QCOMPARE(inbound[QStringLiteral("listen")].toString(), QStringLiteral("127.0.0.1"));
        QCOMPARE(inbound[QStringLiteral("settings")].toObject()[QStringLiteral("address")].toString(), config.targetHost);
        QCOMPARE(inbound[QStringLiteral("settings")].toObject()[QStringLiteral("port")].toInt(), 2222);
        config.targetHost.clear();
        config.targetPort = 0;
        const auto defaults = config.forwardingConfig(QStringLiteral("203.0.113.10"), 22, 22022);
        const auto target =
                defaults[QStringLiteral("inbounds")].toArray().first().toObject()[QStringLiteral("settings")].toObject();
        QCOMPARE(target[QStringLiteral("address")].toString(), QStringLiteral("203.0.113.10"));
        QCOMPARE(target[QStringLiteral("port")].toInt(), 22);
        QVERIFY(config.forwardingConfig(QStringLiteral("203.0.113.10"), 22, 0).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSshManagement)
#include "testSshManagement.moc"
