#ifndef SSHMANAGEMENTCONFIG_H
#define SSHMANAGEMENTCONFIG_H

#include <QJsonObject>
#include <QString>

namespace amnezia
{
    struct SshManagementConfig
    {
        QString transport = QStringLiteral("direct");
        QString targetHost;
        int targetPort = 0;
        QString hostKeySha256;
        QJsonObject xrayConfig;

        bool isConfigured() const;
        bool isValid() const;
        QJsonObject realityOutbound() const;
        QJsonObject forwardingConfig(const QString &publicHost, int sshPort, int localPort) const;
        QJsonObject toJson() const;
        static SshManagementConfig fromJson(const QJsonObject &json);
    };
}

#endif // SSHMANAGEMENTCONFIG_H
