#include "sshManagementConfig.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace amnezia
{
    QJsonObject SshManagementConfig::forwardingConfig(const QString &publicHost, int sshPort, int localPort) const
    {
        const QString host = targetHost.isEmpty() ? publicHost : targetHost;
        const int port = targetPort == 0 ? sshPort : targetPort;
        if (transport != QLatin1String("xray") || !isValid() || host.isEmpty() || port < 1 || port > 65535
            || localPort < 1 || localPort > 65535) {
            return {};
        }
        auto outbound = realityOutbound();
        outbound[QStringLiteral("tag")] = QStringLiteral("management-proxy");
        outbound.remove(QStringLiteral("proxySettings"));
        outbound.remove(QStringLiteral("sendThrough"));
        auto stream = outbound.value(QStringLiteral("streamSettings")).toObject();
        stream.remove(QStringLiteral("sockopt"));
        outbound[QStringLiteral("streamSettings")] = stream;
        const QJsonObject inbound { { QStringLiteral("listen"), QStringLiteral("127.0.0.1") },
                                    { QStringLiteral("port"), localPort },
                                    { QStringLiteral("protocol"), QStringLiteral("dokodemo-door") },
                                    { QStringLiteral("settings"),
                                      QJsonObject { { QStringLiteral("address"), host },
                                                    { QStringLiteral("port"), port },
                                                    { QStringLiteral("network"), QStringLiteral("tcp") } } } };
        return { { QStringLiteral("log"), QJsonObject { { QStringLiteral("loglevel"), QStringLiteral("none") } } },
                 { QStringLiteral("inbounds"), QJsonArray { inbound } },
                 { QStringLiteral("outbounds"), QJsonArray { outbound } } };
    }

    bool SshManagementConfig::isConfigured() const
    {
        return transport != QLatin1String("direct") || !targetHost.isEmpty() || targetPort != 0
                || !hostKeySha256.isEmpty() || !xrayConfig.isEmpty();
    }

    QJsonObject SshManagementConfig::realityOutbound() const
    {
        QJsonObject selected;
        for (const QJsonValue &value : xrayConfig.value(QStringLiteral("outbounds")).toArray()) {
            const QJsonObject outbound = value.toObject();
            const QJsonObject stream = outbound.value(QStringLiteral("streamSettings")).toObject();
            if (outbound.value(QStringLiteral("protocol")).toString() != QLatin1String("vless")
                || stream.value(QStringLiteral("security")).toString() != QLatin1String("reality")) {
                continue;
            }
            if (!selected.isEmpty()) {
                return {};
            }
            selected = outbound;
        }
        return selected;
    }

    bool SshManagementConfig::isValid() const
    {
        if (transport == QLatin1String("direct")) {
            return true;
        }
        if (transport != QLatin1String("xray") || targetPort < 0 || targetPort > 65535) {
            return false;
        }
        static const QRegularExpression fingerprint(QStringLiteral("^SHA256:[A-Za-z0-9+/]{43}$"));
        if (!fingerprint.match(hostKeySha256).hasMatch()) {
            return false;
        }
        const QByteArray encoded = hostKeySha256.mid(7).toLatin1();
        const QByteArray hash = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
        if (hash.size() != 32 || hash.toBase64(QByteArray::OmitTrailingEquals) != encoded) {
            return false;
        }
        const QJsonObject outbound = realityOutbound();
        const QJsonObject stream = outbound.value(QStringLiteral("streamSettings")).toObject();
        const QString network = stream.value(QStringLiteral("network")).toString();
        if (!network.isEmpty() && network != QLatin1String("tcp") && network != QLatin1String("raw")
            && network != QLatin1String("xhttp")) {
            return false;
        }
        const QJsonObject reality = stream.value(QStringLiteral("realitySettings")).toObject();
        if (reality.value(QStringLiteral("publicKey")).toString().isEmpty()
            && reality.value(QStringLiteral("password")).toString().isEmpty()) {
            return false;
        }
        const QJsonArray servers =
                outbound.value(QStringLiteral("settings")).toObject().value(QStringLiteral("vnext")).toArray();
        if (servers.size() != 1) {
            return false;
        }
        const QJsonObject server = servers.first().toObject();
        const int port = server.value(QStringLiteral("port")).toInt();
        const QJsonArray users = server.value(QStringLiteral("users")).toArray();
        return !server.value(QStringLiteral("address")).toString().isEmpty() && port > 0 && port <= 65535
                && users.size() == 1 && !users.first().toObject().value(QStringLiteral("id")).toString().isEmpty();
    }

    QJsonObject SshManagementConfig::toJson() const
    {
        if (!isConfigured()) {
            return {};
        }
        return { { QStringLiteral("transport"), transport },
                 { QStringLiteral("targetHost"), targetHost },
                 { QStringLiteral("targetPort"), targetPort },
                 { QStringLiteral("hostKeySha256"), hostKeySha256 },
                 { QStringLiteral("xrayConfig"), xrayConfig } };
    }

    SshManagementConfig SshManagementConfig::fromJson(const QJsonObject &json)
    {
        SshManagementConfig config;
        if (json.contains(QStringLiteral("transport"))) {
            config.transport = json.value(QStringLiteral("transport")).toString();
        }
        config.targetHost = json.value(QStringLiteral("targetHost")).toString();
        const QJsonValue port = json.value(QStringLiteral("targetPort"));
        if (!port.isUndefined()) {
            config.targetPort = port.isDouble() && port.toDouble() == port.toInt(-1) ? port.toInt(-1) : -1;
        }
        config.hostKeySha256 = json.value(QStringLiteral("hostKeySha256")).toString();
        config.xrayConfig = json.value(QStringLiteral("xrayConfig")).toObject();
        return config;
    }
}
