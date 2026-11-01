#pragma once

#include <QNetworkInterface>

namespace windowsVpnConflict {
inline bool isActiveOfficialTunnel(const QString &alias, QNetworkInterface::InterfaceFlags flags,
                                  const QList<QHostAddress> &addresses)
{
    if (alias.compare(QStringLiteral("AmneziaVPN"), Qt::CaseInsensitive) != 0
        || !flags.testFlag(QNetworkInterface::IsUp)
        || !flags.testFlag(QNetworkInterface::IsRunning)) {
        return false;
    }
    for (const auto &address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isNull()
            && !address.isLoopback() && !address.isLinkLocal()) {
            return true;
        }
    }
    return false;
}

inline bool hasActiveOfficialTunnel()
{
    for (const auto &adapter : QNetworkInterface::allInterfaces()) {
        QList<QHostAddress> addresses;
        for (const auto &entry : adapter.addressEntries()) {
            addresses.append(entry.ip());
        }
        if (isActiveOfficialTunnel(adapter.humanReadableName(), adapter.flags(), addresses)) {
            return true;
        }
    }
    return false;
}
} // namespace windowsVpnConflict
